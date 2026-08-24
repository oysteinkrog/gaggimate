#ifndef GAGGIMATE_SIM

#include "BandDma.h"
#include <esp32-hal-log.h>

namespace {
// gdma_link.c caps one descriptor at GDMA_MAX_BUFFER_SIZE_PER_LINK_ITEM (4095)
// and then rounds that down to the list's buffer alignment, so the worst case
// per item is the smallest value that rounding can produce. Sizing the lists
// off 4095/2 rather than the exact figure keeps this independent of an
// alignment the driver is free to change.
constexpr size_t GDMA_ITEM_BYTES_WORST_CASE = 2048;
} // namespace

BandDma::~BandDma() { uninstall(); }

esp_err_t BandDma::install(size_t maxTransferBytes, size_t burstBytes, DoneFn onDone) {
    if (_rxChan != nullptr) {
        return ESP_OK;
    }
    if (maxTransferBytes == 0 || onDone == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    _onDone = onDone;
    _maxBytes = maxTransferBytes;

    // TX and RX have to be the two halves of one GDMA pair, so the TX is
    // allocated first with its sibling reserved and the RX is then pinned to
    // it. This mirrors esp_async_memcpy_install_gdma_template.
    gdma_channel_alloc_config_t txCfg = {};
    txCfg.direction = GDMA_CHANNEL_DIRECTION_TX;
    txCfg.flags.reserve_sibling = 1;
    esp_err_t err = gdma_new_ahb_channel(&txCfg, &_txChan);
    if (err != ESP_OK) {
        log_w("BandDma: no GDMA TX channel (%s)", esp_err_to_name(err));
        uninstall();
        return err;
    }
    gdma_channel_alloc_config_t rxCfg = {};
    rxCfg.direction = GDMA_CHANNEL_DIRECTION_RX;
    rxCfg.sibling_chan = _txChan;
    err = gdma_new_ahb_channel(&rxCfg, &_rxChan);
    if (err != ESP_OK) {
        log_w("BandDma: no GDMA RX channel (%s)", esp_err_to_name(err));
        uninstall();
        return err;
    }

    // Memory-to-memory needs a trigger id that nothing else is using; the
    // driver publishes the free ones as a mask.
    gdma_trigger_t trigger = GDMA_MAKE_TRIGGER(GDMA_TRIG_PERIPH_M2M, 0);
    uint32_t freeIds = 0;
    err = gdma_get_free_m2m_trig_id_mask(_txChan, &freeIds);
    if (err != ESP_OK || freeIds == 0) {
        log_w("BandDma: no free M2M trigger id");
        uninstall();
        return err == ESP_OK ? ESP_ERR_NOT_FOUND : err;
    }
    trigger.instance_id = __builtin_ctz(freeIds);
    if ((err = gdma_connect(_rxChan, trigger)) != ESP_OK || (err = gdma_connect(_txChan, trigger)) != ESP_OK) {
        log_w("BandDma: M2M connect failed (%s)", esp_err_to_name(err));
        uninstall();
        return err;
    }

    // owner_check off because nothing else touches these descriptors and every
    // mount rewrites the owner bit anyway (gdma_link.c sets dw0.owner =
    // GDMA_LLI_OWNER_DMA on each item it writes), so the check can only cost
    // time. eof_till_data_popped makes the EOF wait until the data has actually
    // left the FIFO, which is what makes the completion callback mean "the
    // bytes are in PSRAM" rather than "the descriptor was consumed".
    gdma_strategy_config_t strategy = {};
    strategy.owner_check = false;
    strategy.auto_update_desc = true;
    strategy.eof_till_data_popped = true;
    gdma_apply_strategy(_txChan, &strategy);
    gdma_apply_strategy(_rxChan, &strategy);

    gdma_transfer_config_t transfer = {};
    transfer.max_data_burst_size = burstBytes;
    transfer.access_ext_mem = true;
    if ((err = gdma_config_transfer(_txChan, &transfer)) != ESP_OK ||
        (err = gdma_config_transfer(_rxChan, &transfer)) != ESP_OK) {
        log_w("BandDma: transfer config failed (%s)", esp_err_to_name(err));
        uninstall();
        return err;
    }
    // Read back rather than assumed: gdma_config_transfer adjusts these from
    // the burst size and the cache line, so the value is only knowable after it.
    gdma_get_alignment_constraints(_rxChan, &_intAlign, &_extAlign);

    const size_t items = maxTransferBytes / GDMA_ITEM_BYTES_WORST_CASE + 2;
    for (int i = 0; i < SLOTS; i++) {
        gdma_link_list_config_t linkCfg = {};
        linkCfg.num_items = items;
        linkCfg.item_alignment = 4; // AHB GDMA descriptor alignment
        linkCfg.flags.check_owner = false;
        linkCfg.flags.items_in_ext_mem = false;

        linkCfg.buffer_alignment = _intAlign ? _intAlign : 4; // TX reads the band buffer, internal SRAM
        if ((err = gdma_new_link_list(&linkCfg, &_txLink[i])) != ESP_OK) {
            log_w("BandDma: TX link list %d failed (%s)", i, esp_err_to_name(err));
            uninstall();
            return err;
        }
        linkCfg.buffer_alignment = _extAlign ? _extAlign : 4; // RX writes the framebuffer, PSRAM
        if ((err = gdma_new_link_list(&linkCfg, &_rxLink[i])) != ESP_OK) {
            log_w("BandDma: RX link list %d failed (%s)", i, esp_err_to_name(err));
            uninstall();
            return err;
        }
    }

    gdma_rx_event_callbacks_t cbs = {};
    cbs.on_recv_eof = rxEofTrampoline;
    if ((err = gdma_register_rx_event_callbacks(_rxChan, &cbs, this)) != ESP_OK) {
        log_w("BandDma: RX EOF callback failed (%s)", esp_err_to_name(err));
        uninstall();
        return err;
    }

    log_i("BandDma: GDMA M2M ready, %d slots x %u items, align int=%u ext=%u, burst=%u", SLOTS,
          static_cast<unsigned>(items), static_cast<unsigned>(_intAlign), static_cast<unsigned>(_extAlign),
          static_cast<unsigned>(burstBytes));
    return ESP_OK;
}

void BandDma::uninstall() {
    if (_rxChan != nullptr) {
        gdma_disconnect(_rxChan);
        gdma_del_channel(_rxChan);
        _rxChan = nullptr;
    }
    if (_txChan != nullptr) {
        gdma_disconnect(_txChan);
        gdma_del_channel(_txChan);
        _txChan = nullptr;
    }
    for (int i = 0; i < SLOTS; i++) {
        if (_txLink[i] != nullptr) {
            gdma_del_link_list(_txLink[i]);
            _txLink[i] = nullptr;
        }
        if (_rxLink[i] != nullptr) {
            gdma_del_link_list(_rxLink[i]);
            _rxLink[i] = nullptr;
        }
    }
    _head = 0;
    _tail = 0;
    _running = false;
    _onDone = nullptr;
}

esp_err_t BandDma::submit(int slot, void *dst, const void *src, size_t bytes, void *arg) {
    if (_rxChan == nullptr || slot < 0 || slot >= SLOTS) {
        return ESP_ERR_INVALID_STATE;
    }
    if (bytes == 0 || bytes > _maxBytes) {
        return ESP_ERR_INVALID_SIZE;
    }

    // The whole transfer is one mounted buffer per direction; the link list
    // splits it across as many descriptors as the item cap needs, sets the
    // owner bit on each, and marks the last TX item EOF+final. The RX items
    // carry no flags, exactly as esp_async_memcpy mounts them: the RX EOF is
    // raised when the TX's EOF data has been received, so the RX list never
    // needs a terminator of its own.
    gdma_buffer_mount_config_t txMount = {};
    txMount.buffer = const_cast<void *>(src);
    txMount.length = bytes;
    txMount.flags.mark_eof = 1;
    txMount.flags.mark_final = 1;
    esp_err_t err = gdma_link_mount_buffers(_txLink[slot], 0, &txMount, 1, nullptr);
    if (err != ESP_OK) {
        return err;
    }
    gdma_buffer_mount_config_t rxMount = {};
    rxMount.buffer = dst;
    rxMount.length = bytes;
    if ((err = gdma_link_mount_buffers(_rxLink[slot], 0, &rxMount, 1, nullptr)) != ESP_OK) {
        return err;
    }

    bool startNow = false;
    portENTER_CRITICAL(&_mux);
    const uint8_t next = static_cast<uint8_t>((_tail + 1) % SLOTS);
    if (next == _head && _running) {
        portEXIT_CRITICAL(&_mux);
        return ESP_ERR_NO_MEM; // ring full; the caller's own throttle should prevent this
    }
    _queue[_tail] = Pending{slot, arg};
    _tail = next;
    if (!_running) {
        _running = true;
        _current = _queue[_head];
        _head = static_cast<uint8_t>((_head + 1) % SLOTS);
        startNow = true;
    }
    portEXIT_CRITICAL(&_mux);

    if (startNow) {
        startSlot(_current.slot);
    }
    return ESP_OK;
}

void BandDma::startSlot(int slot) {
    // RX first: the receiver has to be armed before the sender pushes, or the
    // first bytes have nowhere to land.
    gdma_start(_rxChan, gdma_link_get_head_addr(_rxLink[slot]));
    gdma_start(_txChan, gdma_link_get_head_addr(_txLink[slot]));
}

bool IRAM_ATTR BandDma::rxEofTrampoline(gdma_channel_handle_t, gdma_event_data_t *, void *user) {
    return static_cast<BandDma *>(user)->onRxEof();
}

bool IRAM_ATTR BandDma::onRxEof() {
    void *const arg = _current.arg;

    int nextSlot = -1;
    portENTER_CRITICAL_ISR(&_mux);
    if (_head != _tail) {
        _current = _queue[_head];
        _head = static_cast<uint8_t>((_head + 1) % SLOTS);
        nextSlot = _current.slot;
    } else {
        _running = false;
    }
    portEXIT_CRITICAL_ISR(&_mux);

    // Start the next transfer before running the caller's callback, so the
    // engine is busy again while the callback does its semaphore work rather
    // than after it. gdma_start is documented as ISR-safe.
    if (nextSlot >= 0) {
        startSlot(nextSlot);
    }
    return _onDone != nullptr && _onDone(arg);
}

#endif // GAGGIMATE_SIM
