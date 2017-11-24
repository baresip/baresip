/*************************************************************************/
/* Copyright (c) 2014, Aaron Herting 'qwertos' <aaron@herting.cc>        */
/* Based upon code licensed under the same license by Creytiv.com        */
/*                                                                       */
/* All rights reserved.                                                  */
/*                                                                       */
/* Redistribution and use in source and binary forms, with or without    */
/* modification, are permitted provided that the following conditions    */
/* are met:                                                              */
/*                                                                       */
/* 1. Redistributions of source code must retain the above copyright     */
/* notice, this list of conditions and the following disclaimer.         */
/*                                                                       */
/* 2. Redistributions in binary form must reproduce the above copyright  */
/* notice, this list of conditions and the following disclaimer in the   */
/* documentation and/or other materials provided with the distribution.  */
/*                                                                       */
/* 3. Neither the name of the copyright holder nor the names of its      */
/* contributors may be used to endorse or promote products derived from  */
/* this software without specific prior written permission.              */
/*                                                                       */
/* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS   */
/* "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT     */
/* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR */
/* A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT  */
/* HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,            */
/* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,  */
/* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS */
/* OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED    */
/* AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT           */
/* LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY */
/* WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE           */
/* POSSIBILITY OF SUCH DAMAGE.                                           */
/*************************************************************************/


/**
 * @defgroup dtmfio dtmfio
 *
 * DTMF input/output module
 *
 *
 * # DTMFIO Module
 *
 * ## Description
 *
 * Writes received dtmf button presses to a FIFO located at /tmp/dtmf.out.
 *
 * Also, will write an 'E' when a call is established and an 'F' when the
 * call is finished.
 *
 * ## To Do
 *
 * +   Proper error handling
 * +   Using a dtmf.in file, be able to send DTMF signals
 * +   Use a filename specified by the user in the config file
 * +   Clean up build output so there aren't errors regarding unused vars
 */

#include <unistd.h>
#include <stdio.h>
#include <re.h>
#include <baresip.h>
#include <sys/types.h>
#include <sys/stat.h>


static FILE *fd;
static const char *DTMF_OUT = "/tmp/dtmf.out";


static void dtmf_handler(struct call *call, char key, void *arg)
{
	(void)call;
	(void)arg;

	if ( key != 0 ) {
		fprintf(fd, "%c", key);
		fflush(fd);
	}
}


static void ua_event_handler(struct ua *ua,
			     enum ua_event ev,
			     struct call *call,
			     const char *prm,
			     void *arg )
{
	(void)ua;
	(void)prm;
	(void)arg;

	if ( ev == UA_EVENT_CALL_ESTABLISHED ) {
		fprintf(fd, "E");
		fflush(fd);
		call_set_handlers( call, NULL, dtmf_handler, NULL);
	}

	if (ev == UA_EVENT_CALL_CLOSED ) {
		fprintf(fd, "F");
		fflush(fd);
	}
}


static int module_init(void)
{
	if ( mkfifo( DTMF_OUT, S_IWUSR | S_IRUSR ) ) {
		int err = errno;
		warning("Creation of the FIFO errored."
		      " This might cause issues. (%m)\n", err);
		return err;
	}

	fd = fopen( DTMF_OUT , "w+" );

	if ( fd == 0 ){
		warning("Opening of the FIFO errored."
		      " This might cause issues.\n");
	}

	uag_event_register( ua_event_handler, NULL );

	return 0;
}


static int module_close(void)
{
	uag_event_unregister(ua_event_handler);

	if (fd) {
		fclose(fd);
		fd = NULL;
	}

	unlink(DTMF_OUT);

	return 0;
}


const struct mod_export DECL_EXPORTS(dtmfio) = {
	"dtmfio",
	"application",
	module_init,
	module_close
};
