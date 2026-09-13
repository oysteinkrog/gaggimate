#ifndef GM_GRADIENT_PICK_TRANSACTION_H
#define GM_GRADIENT_PICK_TRANSACTION_H

// One gradient selection, as one settings transaction (gm-nov3.17).
//
// Settings.h is explicit that Settings::lock() orders whole transactions and
// not fields: Property::get and Property::set stay lock-free, so two writes
// that must agree with each other, or a read the following write depends on,
// have to sit inside one Settings::Guard. A gradient pick is exactly that
// shape. The picker resolves the tapped ref against the library that is
// stored right now, and only then hands it to the category that opened the
// picker, which writes it into the per-animation map or into the global ref
// and its legacy mirror. Between the resolve and the write, a web save's
// batchUpdate runs on another task and can delete the library entry the
// resolve just approved, so a picker that releases the lock in between
// publishes a ref that names nothing.
//
// The fix is this function rather than a second Guard at each call site,
// because a Guard at a call site is a thing a later reader can shorten
// without noticing what it was for. Here the guard is the whole function
// body: the resolve, the target check and the assignment are the only
// statements in its scope, and there is no way to keep one of them and drop
// the others. Everything with a side effect outside Settings (popping the
// picker's pages, redrawing) belongs to the caller and stays outside.
//
// Ops is the seam, so the transaction can be run against something other
// than the device's Settings:
//
//   using Guard  -- RAII lock over whatever guarded() returns
//   guarded()    -- what the Guard is constructed from
//   refResolves(ref) -- does this ref name a gradient in the stored data?
//   targetValid()    -- is the slot the picker was opened for still writable?
//   assign(ref)      -- write the selection
//
// Production's Ops lives in CatGradientPicker.cpp and uses Settings::Guard,
// which is recursive, so an assign() that takes its own guard for a
// read-modify-write (animGradientPicked does, for the whole map string) still
// nests inside this one.

namespace settingsui {

enum class GradientPickOutcome {
    Assigned,
    // The ref named nothing in the library that is stored now. The library
    // entry was deleted from the web UI after the page listing it was built.
    RefGone,
    // The slot the picker was opened for stopped being worth editing (an
    // animation id that has left the roster). Nothing is written and nothing
    // is marked touched, so the caller closes on the value that was in force.
    TargetGone,
};

template <typename Ops> GradientPickOutcome gradientPickTransaction(Ops &ops, const char *ref) {
    typename Ops::Guard guard(ops.guarded());
    // "" is Global, which names no gradient and so resolves against nothing.
    const bool namesAGradient = ref != nullptr && ref[0] != '\0';
    if (namesAGradient && !ops.refResolves(ref)) {
        return GradientPickOutcome::RefGone;
    }
    if (!ops.targetValid()) {
        return GradientPickOutcome::TargetGone;
    }
    ops.assign(namesAGradient ? ref : "");
    return GradientPickOutcome::Assigned;
}

} // namespace settingsui

#endif // GM_GRADIENT_PICK_TRANSACTION_H
