#include <stdio.h>
#include <check.h>

static int selftest(int a,int b)
{
    return a+b;
}

START_TEST (self_test)
{
     fail_unless(selftest(2,3) == 5, "self test borked");
}
END_TEST

int main(void)
{
    int nf;

    Suite *s1 = suite_create("Baresip");
    
    TCase *tc1_1 = tcase_create("Self test");
    SRunner *sr = srunner_create(s1);

    suite_add_tcase(s1, tc1_1);
    tcase_add_test(tc1_1, self_test);

    srunner_run_all(sr, CK_ENV);
    nf = srunner_ntests_failed(sr);
    srunner_free(sr);

    return nf == 0 ? 0 : 1;

}
