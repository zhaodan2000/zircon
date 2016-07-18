// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <assert.h>
#include <magenta/syscalls.h>
#include <unittest/unittest.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

mx_handle_t _pipe[4];

/**
 * Message pipe tests with wait multiple.
 *
 * Tests signal state persistence and various combinations of states on multiple handles.
 *
 * Test sequence (may not be exact due to concurrency):
 *   1. Create 2 pipes and start a reader thread.
 *   2. Reader blocks wait on both pipes.
 *   3. Write to both pipes and yield.
 *   4. Reader wake up with pipe 1 and pipe 2 readable.
 *   5. Reader reads from pipe 1, and calls wait again.
 *   6. Reader should wake up immediately, with pipe 1 not readable and pipe 2 readable.
 *   7. Reader blocks on wait.
 *   8. Write to pipe 1 and yield.
 *   9. Reader wake up with pipe 1 readable and reads from pipe 1.
 *  10. Reader blocks on wait.
 *  11. Write to pipe 2 and close both pipes, then yield.
 *  12. Reader wake up with pipe 2 closed and readable.
 *  13. Read from pipe 2 and wait.
 *  14. Reader wake up with pipe 2 closed, closes both pipes and exit.
 */

static int reader_thread(void* arg) {
    const unsigned int index = 2;
    mx_handle_t* pipe = &_pipe[index];
    mx_status_t status;
    mx_signals_state_t states[2];
    mx_signals_t signals = MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED;
    unsigned int packets[2] = {0, 0};
    bool closed[2] = {false, false};
    do {
        status = mx_handle_wait_many(2, pipe, &signals, MX_TIME_INFINITE, NULL, states);
        ASSERT_EQ(status, NO_ERROR, "error from mx_handle_wait_many");
        uint32_t data;
        uint32_t num_bytes = sizeof(uint32_t);
        if (states[0].satisfied & MX_SIGNAL_READABLE) {
            status = mx_message_read(pipe[0], &data, &num_bytes, NULL, 0u, 0u);
            ASSERT_EQ(status, NO_ERROR, "error while reading message");
            packets[0] += 1;
        } else if (states[1].satisfied & MX_SIGNAL_READABLE) {
            status = mx_message_read(pipe[1], &data, &num_bytes, NULL, 0u, 0u);
            ASSERT_EQ(status, NO_ERROR, "error while reading message");
            packets[1] += 1;
        } else {
            if (states[0].satisfied & MX_SIGNAL_PEER_CLOSED)
                closed[0] = true;
            if (states[1].satisfied & MX_SIGNAL_PEER_CLOSED)
                closed[1] = true;
        }
    } while (!closed[0] || !closed[1]);
    assert(packets[0] == 3);
    assert(packets[1] == 2);
    mx_thread_exit();
    return 0;
}

mx_signals_t get_satisfied_signals(mx_handle_t handle) {
    mx_signals_state_t signals_state = {0};
    mx_status_t status = mx_handle_wait_one(handle, 0u, 0u, &signals_state);
    assert(status == ERR_BAD_STATE);  // "Unsatisfiable".
    return signals_state.satisfied;
}

mx_signals_t get_satisfiable_signals(mx_handle_t handle) {
    mx_signals_state_t signals_state = {0};
    mx_status_t status = mx_handle_wait_one(handle, 0u, 0u, &signals_state);
    assert(status == ERR_BAD_STATE);  // "Unsatisfiable".
    return signals_state.satisfiable;
}

bool message_pipe_test(void) {
    BEGIN_TEST;

    mx_status_t status;

    mx_handle_t h[2];
    status = mx_message_pipe_create(h, 0);
    ASSERT_EQ(status, 0, "error in message pipe create");

    ASSERT_EQ(get_satisfied_signals(h[0]), MX_SIGNAL_WRITABLE, "");
    ASSERT_EQ(get_satisfied_signals(h[1]), MX_SIGNAL_WRITABLE, "");
    ASSERT_EQ(get_satisfiable_signals(h[0]),
              MX_SIGNAL_READABLE | MX_SIGNAL_WRITABLE | MX_SIGNAL_PEER_CLOSED, "");
    ASSERT_EQ(get_satisfiable_signals(h[1]),
              MX_SIGNAL_READABLE | MX_SIGNAL_WRITABLE | MX_SIGNAL_PEER_CLOSED, "");

    _pipe[0] = h[0];
    _pipe[2] = h[1];

    status = mx_message_pipe_create(h, 0);
    ASSERT_EQ(status, 0, "error in message pipe create");

    _pipe[1] = h[0];
    _pipe[3] = h[1];

    static const uint32_t write_data = 0xdeadbeef;
    status = mx_message_write(_pipe[0], &write_data, sizeof(uint32_t), NULL, 0u, 0u);
    ASSERT_EQ(status, NO_ERROR, "error in message write");
    ASSERT_EQ(get_satisfied_signals(_pipe[0]), MX_SIGNAL_WRITABLE, "");
    ASSERT_EQ(get_satisfied_signals(_pipe[2]), MX_SIGNAL_READABLE | MX_SIGNAL_WRITABLE, "");

    const char* reader = "reader";
    mx_handle_t thread = mx_thread_create(reader_thread, NULL, reader, strlen(reader) + 1);
    ASSERT_GE(thread, 0, "error in thread create");

    status = mx_message_write(_pipe[1], &write_data, sizeof(uint32_t), NULL, 0u, 0u);
    ASSERT_EQ(status, NO_ERROR, "error in message write");

    usleep(1);

    status = mx_message_write(_pipe[0], &write_data, sizeof(uint32_t), NULL, 0u, 0u);
    ASSERT_EQ(status, NO_ERROR, "error in message write");

    status = mx_message_write(_pipe[0], &write_data, sizeof(uint32_t), NULL, 0u, 0u);
    ASSERT_EQ(status, NO_ERROR, "error in message write");

    usleep(1);

    status = mx_message_write(_pipe[1], &write_data, sizeof(uint32_t), NULL, 0u, 0u);
    ASSERT_EQ(status, NO_ERROR, "error in message write");

    mx_handle_close(_pipe[1]);
    ASSERT_EQ(get_satisfied_signals(_pipe[3]), MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED, "");
    ASSERT_EQ(get_satisfiable_signals(_pipe[3]), MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED, "");

    usleep(1);
    mx_handle_close(_pipe[0]);

    mx_handle_wait_one(thread, MX_SIGNAL_SIGNALED, MX_TIME_INFINITE, NULL);

    // Since the the other side of _pipe[3] is closed, and the read thread read everything from it,
    // the only satisfied/satisfiable signals should be "peer closed".
    ASSERT_EQ(get_satisfied_signals(_pipe[3]), MX_SIGNAL_PEER_CLOSED, "");
    ASSERT_EQ(get_satisfiable_signals(_pipe[3]), MX_SIGNAL_PEER_CLOSED, "");

    mx_handle_close(_pipe[2]);
    mx_handle_close(_pipe[3]);

    END_TEST;
}

bool message_pipe_read_error_test(void) {
    BEGIN_TEST;
    mx_handle_t pipe[2];
    mx_status_t status = mx_message_pipe_create(pipe, 0);
    ASSERT_EQ(status, 0, "error in message pipe create");

    // Read from an empty message pipe.
    status = mx_message_read(pipe[0], NULL, 0u, NULL, 0u, 0u);
    ASSERT_EQ(status, ERR_BAD_STATE, "read on empty non-closed pipe produced incorrect error");

    char data = 'x';
    status = mx_message_write(pipe[1], &data, 1u, NULL, 0u, 0u);
    ASSERT_EQ(status, NO_ERROR, "write failed");

    mx_handle_close(pipe[1]);

    // Read a message with the peer closed, should yield the message.
    char read_data = '\0';
    uint32_t read_data_size = 1u;
    status = mx_message_read(pipe[0], &read_data, &read_data_size, NULL, 0u, 0u);
    ASSERT_EQ(status, NO_ERROR, "read failed with peer closed but message in the pipe");
    ASSERT_EQ(read_data_size, 1u, "read returned incorrect number of bytes");
    ASSERT_EQ(read_data, 'x', "read returned incorrect data");

    // Read from an empty pipe with a closed peer, should yield a channel closed error.
    status = mx_message_read(pipe[0], NULL, 0u, NULL, 0u, 0u);
    ASSERT_EQ(status, ERR_CHANNEL_CLOSED, "read on empty closed pipe produced incorrect error");

    // Waiting for readability should yield a bad state error.
    status = mx_handle_wait_one(pipe[0], MX_SIGNAL_READABLE, 0u, NULL);
    ASSERT_EQ(status, ERR_BAD_STATE, "waiting for readability should not succeed");

    END_TEST;
}

BEGIN_TEST_CASE(message_pipe_tests)
RUN_TEST(message_pipe_test)
RUN_TEST(message_pipe_read_error_test)
END_TEST_CASE(message_pipe_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
