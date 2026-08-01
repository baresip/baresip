/**
 * @file test/peerconn_internal.h
 *
 * Shared entry points for peer-connection test groups.
 */

struct gather_wait {
	unsigned calls;
	bool ready;
};

void peerconn_validation_gather_handler(void *arg);
int peerconn_test_menc_transport(const struct menc *menc, bool members);
int peerconn_test_transport_identity(const struct menc *menc);
int peerconn_test_transport_suite(const struct menc *menc);
int peerconn_test_data_contracts(const struct mnat *mnat,
				 const struct menc *menc);
int peerconn_test_answer_retry(const struct mnat *mnat,
			       const struct menc *menc);
