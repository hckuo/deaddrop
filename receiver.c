#include "util.h"

struct state {
    char *buffer;
    struct Node *probing_set;
    int interval;
    int wait_cycles_between_measurements;
    bool debug;
};

void init_state(struct state *state, int argc, char **argv) {
    int n = CACHE_WAYS_L1;
    int o = 6;              // log_2(64), where 64 is the line size
    int s = 6;              // log_2(64), where 64 is the number of cache sets

    int two_o_s = ipow(2, o + s);       // 4096
    int b = 2 * n * two_o_s;                // 32,768

    state->buffer = malloc((size_t) b);

    // Set some default values; need to be tuned up
    state->interval = 160;
    state->wait_cycles_between_measurements = 30;
    state->debug = false;
    state->probing_set = NULL;

    for (int i = 0; i < 2 * n; i++) {
        ADDR_PTR addr = (ADDR_PTR) (state->buffer + two_o_s * i);
        if (get_cache_set_index(addr) == 0x0) {
            append_string_to_linked_list(&state->probing_set, addr);
        }
    }

    int option;
    while ((option = getopt(argc, argv, "di:w:")) != -1) {
        switch (option) {
            case 'd':
                state->debug = true;
                break;
            case 'i':
                state->interval = atoi(optarg);
                break;
            case 'w':
                state->wait_cycles_between_measurements = atoi(optarg);
                break;
            case '?':
                fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
                exit(1);
            default:
                exit(1);
        }
    }
}

bool detect_bit(struct state *state, bool first_time) {
    clock_t start_t, curr_t;
    start_t = clock();
    curr_t = start_t;

    int misses = 0;
    int hits = 0;
    int total_measurements = 0;
    // This is high because the misses caused by
    // clflush usually take longer than 150 cycles
    int misses_time_threshold = 150;
    while ((curr_t - start_t) < state->interval) {

        struct Node *current = state->probing_set;
        while (current != NULL && (curr_t - start_t) < state->interval) {
            ADDR_PTR addr = current->addr;
            CYCLES time = measure_one_block_access_time(addr);
            // printf("Time: %d\n", time);

            curr_t = clock();
            current = current->next;

            // Exclude disk misses
            if (time < 1000) {
                total_measurements++;
                if (time > misses_time_threshold) {
                    misses++;
                } else {
                    hits++;
                }
            }

            // Busy loop to give time to the sender
            for (int junk = 0; junk < state->wait_cycles_between_measurements &&
                               (curr_t - start_t) < state->interval; junk++) {
                curr_t = clock();
            }
        }
    }

    double miss_threshold = (float) total_measurements / 8.0;
    if (first_time && misses > miss_threshold) {
        start_t = clock();
        double expected_for_a_one = (float) total_measurements / 1.2;
        double ratio = (1 - misses / expected_for_a_one);
        double tres = state->interval * ratio;

        while (clock() - start_t < tres) {}

        if (state->debug) {
            printf("\nSynchronizing forward of %.2f\n", ratio);
        }
    }

    bool ret = ((first_time && misses > miss_threshold) ||
            (!first_time && misses > (float) total_measurements / 2.0));

    if (state->debug) {
        printf("Misses: %d out of %d --> %d\n", misses, total_measurements, ret);
    }

    return ret;
}

// This is the only hardcoded variable which defines the max size of a message
// to be the same as the max size of the message in the starter code of the sender.
static const int max_buffer_len = 128 * 8;

int main(int argc, char **argv) {
    // Setup code
    struct state state;
    init_state(&state, argc, argv);

    printf("Press enter to begin listening \n ");
    getchar();

    char msg_ch[max_buffer_len + 1];

    int start_sequence = 4;
    bool first_time = true;

    bool detected;
    bool previous = true;

    bool receiving = 1;
    while (receiving) {
        detected = detect_bit(&state, first_time);

        // Check if the start sequence has been fully detected
        // and we are synchronized with the sender
        if (start_sequence == 0 && detected == 1 && previous == 1) {
            if (state.debug) {
                printf("Start sequence detected.\n\n");
            }

            int binary_msg_len = 0;
            int strike_zeros = 0;
            for (int i = 0; i < max_buffer_len; i++) {
                binary_msg_len++;

                if (detect_bit(&state, first_time)) {
                    msg_ch[i] = '1';
                    strike_zeros = 0;

                } else {
                    msg_ch[i] = '0';

                    if (++strike_zeros >= 8 && i % 8 == 0) {
                        if (state.debug) {
                            printf("String finished\n");
                        }
                        break;
                    }
                }
            }

            msg_ch[binary_msg_len - 7] = '\0';
            if (state.debug) {
                printf("Binary string received %s\n", msg_ch);
            }

            int ascii_msg_len = binary_msg_len / 8;
            char msg[ascii_msg_len];
            printf("> %s\n", conv_char(msg_ch, ascii_msg_len, msg));
        }

        // Check if the first part of the start sequence has been already
        // fully detected, but we are waiting for the trailing zeros
        else if (start_sequence == 0 && detected != previous) {
            // Do nothing in this case
        }

        // Look for the first part of the start sequence (1010)
        else if (start_sequence > 0 && detected != previous) {
            start_sequence--;
            first_time = false;
        }
        else {
            start_sequence = 4;
            first_time = true;
        }
        previous = detected;
    } // Main while loop
    return 0;
}
