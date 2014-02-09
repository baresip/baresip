/**
 * @file ecrt.cpp  ECRT wrapper for Symbian OS
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <stdlib.h>
#include <stdio.h>
#include <e32base.h>
#include <e32cons.h>
#include <sys/reent.h>

_LIT(KAppName, "baresip");
extern "C" int main(int argc, char** argv);


TInt E32Main()
{
	__UHEAP_MARK;
	CTrapCleanup* cleanup = CTrapCleanup::New();
	int ret = 0;
	TRAPD(err, ret = main(0, NULL));
	if (err)
		printf("main left with error %d\n", err);
	if (ret)
		printf("main returned %d\n", ret);
	__ASSERT_ALWAYS(!err, User::Panic(KAppName, err));
	CloseSTDLIB();
	delete cleanup;
	__UHEAP_MARKEND;
	return err;
}
