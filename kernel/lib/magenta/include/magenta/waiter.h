// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <kernel/spinlock.h>

#include <magenta/types.h>
#include <magenta/io_port_dispatcher.h>

#include <utils/intrusive_single_list.h>

class Handle;
class WaitEvent;

// Magenta Waiter
//
//  Provides the interface between the syscall layer and the kernel object layer
//  that allows waiting for object state changes. It connects the waitee (which
//  owns the Waiter object) and (possibly) many waiters.
//
//  The waitee uses Signal/ClearSignal to inform the waiters of state changes.
//
//  The Waiter has two styles for notifying waiters. They are mutually exclusive.
//
//  In the examples that follow, assume a waitee pointed by |handle| and
//  some |signals| to wait for.
//
//  Style 1: Using BeginWait / FinishWait. Assume an existing |event|
//
//      auto waiter = handle->dispatcher()->get_waiter();
//      waiter->BeginWait(&event, handle, signals, 0);
//
//      event.Wait(timeout);
//      waiter->FinishWait(&event);
//
//  Style 2: Using IOPorts. Assume an existing |io_port|.
//
//      auto waiter = handle->dispatcher()->get_waiter();
//      waiter->BindIOPOrt(io_port, key, signals);
//
//      IOP_Packet pk;
//      io_port->Wait(&pk);
//

class Waiter {
public:
    // Note: The initial state can also be set using SetInitialSignalsState() if the default
    // constructor must be used for some reason.
    explicit Waiter(mx_signals_state_t signals_state = mx_signals_state_t{0u, 0u});

    Waiter(const Waiter& o) = delete;
    Waiter& operator=(const Waiter& o) = delete;

    // Set the initial signals state. This is an alternative to provide the initial signals state to
    // the constructor. This does no locking and does not notify anything.
    void set_initial_signals_state(mx_signals_state_t signals_state) {
        signals_state_ = signals_state;
    }

    // Start an event-based wait.
    mx_status_t BeginWait(WaitEvent* event, Handle* handle, mx_signals_t signals, uint64_t context);

    // End an event-based wait.
    mx_signals_state_t FinishWait(WaitEvent* event);

    // Register IO Port for state changes.
    bool BindIOPort(utils::RefPtr<IOPortDispatcher> io_port, uint64_t key, mx_signals_t signals);

    // Cancel a pending wait started with BeginWait.
    bool CancelWait(Handle* handle);

    // Notify others of a change in state (possibly waking them). (Clearing satisfied signals or
    // setting satisfiable signals should not wake anyone.) Returns true if some thread was awoken.
    bool UpdateState(mx_signals_t satisfied_set_mask,
                     mx_signals_t satisfied_clear_mask,
                     mx_signals_t satisfiable_set_mask,
                     mx_signals_t satisfiable_clear_mask,
                     bool yield);

    bool UpdateSatisfied(mx_signals_t set_mask, mx_signals_t clear_mask, bool yield) {
        return UpdateState(set_mask, clear_mask, 0u, 0u, yield);
    }

private:
    struct WaitNode {
        WaitNode* next;
        WaitEvent* event;
        Handle* handle;
        mx_signals_t signals;
        uint64_t context;

        void list_set_next(WaitNode* node) {
            next = node;
        }
        WaitNode* list_next() {
            return next;
        }
    };

    bool SignalStateChange_NoLock();

    bool SendIOPortPacket_NoLock(IOPortDispatcher* io_port, mx_signals_t signals);

    spin_lock_t lock_;

    // Active waiters are elements in |nodes_|.
    utils::SinglyLinkedList<WaitNode> nodes_;

    // mojo-style signaling.
    mx_signals_state_t signals_state_;

    // io port style signaling.
    utils::RefPtr<IOPortDispatcher> io_port_;
    mx_signals_t io_port_signals_;
    uint64_t io_port_key_;
};
