#ifndef GAGGIMATE_SIM

#include "BandDma.h"
#include <esp32-hal-log.h>
#include <esp_timer.h>

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
    //
    // isr_cache_safe is deliberately left off. Setting it would put the
    // driver's interrupt in IRAM, but the handler below finishes by starting
    // the next queued band, and both gdma_start and gdma_link_get_head_addr
    // live in flash -- so an IRAM interrupt would call into unmapped memory
    // during a flash write, which is worse than what it fixes. What it fixes
    // is also smaller than it looks: spi_flash's cache_utils calls
    // esp_intr_noniram_disable before it turns the cache off, so a non-IRAM
    // interrupt is masked rather than fired, and the cost of a flash write
    // landing mid-frame is a deferred completion, not a crash. The render task
    // blocks on that band's slot for as long as the write takes and the frame
    // stutters. Making this genuinely cache-safe means caching the head
    // addresses here and poking the start register directly, which is worth
    // doing only if a stutter during a settings save ever matters.
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

    // Sized for the plain contiguous submit() (one buffer, split at the item
    // cap) but it also has to cover submitRows()'s worst case: MAX_ROW_GROUPS
    // single-row groups, each small enough to need only one item apiece. That
    // worst case is maxTransferBytes's own row count / 2 (interlacing owns
    // half the rows), and items/rowcount both scale with maxTransferBytes, so
    // the margin is a fixed ratio rather than a one-off fit: solving
    // rows/2 <= items for BAND_H*960 = maxTransferBytes gives headroom up to
    // BAND_H=64 before this formula would need to grow. Checked here rather
    // than assumed because submitRows() enforces MAX_ROW_GROUPS as a hard cap
    // and returns an error instead of overrunning the list either way.
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
    // owner bit on each, and marks the last item of each direction final.
    //
    // Only TX carries mark_eof. The RX EOF interrupt is raised when the data
    // the TX tagged has been received, not by anything on the RX descriptors
    // themselves, which is why esp_async_memcpy mounts its RX side with no
    // flags at all.
    //
    // It does not set mark_final either, and this does. Without it the last RX
    // item's next pointer wraps to the head of its own list, so nothing in the
    // descriptor chain stops the receiver from walking back to the start; what
    // stops it is that TX has finished sending and no more bytes arrive. That
    // is almost certainly why the reference gets away with it, but it rests on
    // RX advancing only when it has data rather than eagerly following the
    // chain, and that is an inference about the hardware rather than something
    // the driver states. A null terminator costs nothing and does not need the
    // inference to hold.
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
    rxMount.flags.mark_final = 1;
    if ((err = gdma_link_mount_buffers(_rxLink[slot], 0, &rxMount, 1, nullptr)) != ESP_OK) {
        return err;
    }

    return queueSlot(slot, arg);
}

esp_err_t BandDma::submitRows(int slot, void *dstBase, const void *srcBase, const uint32_t *rowOffsets, int n,
                              size_t groupBytes, void *arg) {
    if (_rxChan == nullptr || slot < 0 || slot >= SLOTS) {
        return ESP_ERR_INVALID_STATE;
    }
    if (n <= 0 || n > MAX_ROW_GROUPS || groupBytes == 0 || static_cast<size_t>(n) * groupBytes > _maxBytes) {
        return ESP_ERR_INVALID_SIZE;
    }

    // Each row group is its own entry in the mount array rather than one
    // contiguous buffer, which is what keeps the rows between them off this
    // transfer entirely -- see this method's doc comment in the header.
    // mark_eof/mark_final only on the LAST entry: the same rule submit()
    // follows for its one entry, just applied at the end of a longer array.
    gdma_buffer_mount_config_t txMount[MAX_ROW_GROUPS] = {};
    gdma_buffer_mount_config_t rxMount[MAX_ROW_GROUPS] = {};
    for (int i = 0; i < n; i++) {
        txMount[i].buffer = const_cast<uint8_t *>(static_cast<const uint8_t *>(srcBase)) + rowOffsets[i];
        txMount[i].length = groupBytes;
        rxMount[i].buffer = static_cast<uint8_t *>(dstBase) + rowOffsets[i];
        rxMount[i].length = groupBytes;
    }
    txMount[n - 1].flags.mark_eof = 1;
    txMount[n - 1].flags.mark_final = 1;
    rxMount[n - 1].flags.mark_final = 1;

    esp_err_t err = gdma_link_mount_buffers(_txLink[slot], 0, txMount, static_cast<size_t>(n), nullptr);
    if (err != ESP_OK) {
        return err;
    }
    if ((err = gdma_link_mount_buffers(_rxLink[slot], 0, rxMount, static_cast<size_t>(n), nullptr)) != ESP_OK) {
        return err;
    }

    return queueSlot(slot, arg);
}

esp_err_t BandDma::queueSlot(int slot, void *arg) {
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
    _xferStartUs = esp_timer_get_time();
    // RX first: the receiver has to be armed before the sender pushes, or the
    // first bytes have nowhere to land.
    gdma_start(_rxChan, gdma_link_get_head_addr(_rxLink[slot]));
    gdma_start(_txChan, gdma_link_get_head_addr(_txLink[slot]));
}

void BandDma::xferStats(uint32_t *count, uint32_t *sumUs, uint32_t *maxUs, uint32_t *over256,
                        uint32_t *over512) const {
    if (count != nullptr) {
        *count = _xferCount;
    }
    if (sumUs != nullptr) {
        *sumUs = _xferSumUs;
    }
    if (maxUs != nullptr) {
        *maxUs = _xferMaxUs;
    }
    if (over256 != nullptr) {
        *over256 = _xferOver256;
    }
    if (over512 != nullptr) {
        *over512 = _xferOver512;
    }
}

void BandDma::xferStatsReset() {
    _xferCount = 0;
    _xferSumUs = 0;
    _xferMaxUs = 0;
    _xferOver256 = 0;
    _xferOver512 = 0;
}

bool IRAM_ATTR BandDma::rxEofTrampoline(gdma_channel_handle_t, gdma_event_data_t *, void *user) {
    return static_cast<BandDma *>(user)->onRxEof();
}

bool IRAM_ATTR BandDma::onRxEof() {
    // Duration of the transfer that just finished, taken before anything can
    // overwrite the start stamp (starting the next slot below does).
    const uint32_t dt = static_cast<uint32_t>(esp_timer_get_time() - _xferStartUs);
    _xferCount = _xferCount + 1;
    _xferSumUs = _xferSumUs + dt;
    if (dt > _xferMaxUs) {
        _xferMaxUs = dt;
    }
    if (dt > 256) {
        _xferOver256 = _xferOver256 + 1;
        if (dt > 512) {
            _xferOver512 = _xferOver512 + 1;
        }
    }

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
