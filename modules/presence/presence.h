/**
 * @file presence.h Presence module interface
 *
 * Copyright (C) 2010 Creytiv.com
 */

int  subscriber_init(void);
void subscriber_close(void);


int  notifier_init(void);
void notifier_close(void);
void notifier_update_status(struct ua *ua);


int  publisher_init(void);
void publisher_close(void);
void publisher_update_status(struct ua *ua);
