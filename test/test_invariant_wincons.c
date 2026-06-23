#include <check.h>
#include <stdlib.h>
#include <windows.h>
#include <stdio.h>
#include "modules/wincons/wincons.c"

START_TEST(test_buffer_read_never_exceeds_declared_length)
{
    // Invariant: ReadConsoleInput never writes more records than buffer size
    const int payload_counts[] = {
        10,  // Exploit case: exceeds buffer size by 2.5x
        4,   // Boundary case: exactly buffer size
        2    // Valid input: within buffer size
    };
    int num_payloads = sizeof(payload_counts) / sizeof(payload_counts[0]);

    for (int i = 0; i < num_payloads; i++) {
        INPUT_RECORD buf[4];
        DWORD count = 0;
        HANDLE hstdin = GetStdHandle(STD_INPUT_HANDLE);
        
        // Set console mode to enable reading
        SetConsoleMode(hstdin, ENABLE_PROCESSED_INPUT);
        
        // Create test input events
        INPUT_RECORD test_events[10];
        for (int j = 0; j < payload_counts[i]; j++) {
            test_events[j].EventType = KEY_EVENT;
            test_events[j].Event.KeyEvent.bKeyDown = TRUE;
            test_events[j].Event.KeyEvent.uChar.AsciiChar = 'A' + (j % 26);
        }
        
        // Write test events to console input buffer
        DWORD written;
        WriteConsoleInput(hstdin, test_events, payload_counts[i], &written);
        
        // Call the actual vulnerable function
        ReadConsoleInput(hstdin, buf, RE_ARRAY_SIZE(buf), &count);
        
        // Security check: count must never exceed buffer size
        ck_assert_msg(count <= RE_ARRAY_SIZE(buf),
                     "Buffer overflow: read %lu records into buffer of size %zu",
                     count, RE_ARRAY_SIZE(buf));
        
        // Additional safety check: ensure we didn't corrupt stack
        for (DWORD j = 0; j < count && j < RE_ARRAY_SIZE(buf); j++) {
            ck_assert(buf[j].EventType == KEY_EVENT);
        }
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_buffer_read_never_exceeds_declared_length);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}