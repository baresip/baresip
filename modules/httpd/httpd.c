/**
 * @file httpd.c Webserver UI module
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <stdlib.h>
#include <string.h>
#include <re.h>
#include <baresip.h>

static struct http_sock *httpsock;

struct snapshot_arg
{
	char	*filename;
	char	*preview_fn;
	char	fmt;   // 'o' || 'p'
	char	com2;  // 0 = nothing 1 = preview
};
	
/*
 simple argument string parser
 */
struct key_value
{
	char	*key;
	char	*value;
};

static struct key_value * parse_prm(char *prm);

#define BARESIP_HTTP_HEAD  "<html>\n<head>\n<title>Baresip v"BARESIP_VERSION"</title>\n</head>\n"

static int html_print_head(struct re_printf *pf, void *unused)
{
	(void)unused;

	return re_hprintf(pf,BARESIP_HTTP_HEAD);
}


static int html_print_cmd(struct re_printf *pf, const struct http_msg *req)
{
	struct pl params;

	if (!pf || !req)
		return EINVAL;

	if (pl_isset(&req->prm)) {
		params.p = req->prm.p + 1;
		params.l = req->prm.l - 1;
	}
	else {
		params.p = "h";
		params.l = 1;
	}

	return re_hprintf(pf,
			  "%H"
			  "<body>\n"
			  "<pre>\n"
			  "%H"
			  "</pre>\n"
			  "</body>\n"
			  "</html>\n",
			  html_print_head, NULL,
			  ui_input_pl, &params);
}

const char * err_msg = BARESIP_HTTP_HEAD
	"<body><pre>\n"
	"HTTP snapshot error\n"
	"</pre>\n"
	"</body>\n";

//128 for http tegs + 128 for filename
const char ok_msg[256];

// it's no good
struct cmd *cmd_find_by_key(char key);

static int html_save_img(struct re_printf *pf, const struct http_msg *msg)
{
	int  err,i;
	char *prm,*strok;
	const struct cmd *cmd;

	char fn_key=0;
	char *filename=NULL, *prev_filename=NULL;
	struct key_value * kv;

	if (!pf || !msg) return EINVAL;
	if (!pl_isset(&msg->prm)) return EINVAL;

	pl_strdup(&prm,&msg->prm);

	kv = parse_prm(prm);

	i=0;
	while(kv[i].key!=NULL)
	{
		// file type and name
		if (strcmp(kv[i].key,"o")==0)
		{
			filename = kv[i].value;
			if (strstr(filename,".jpg") || strstr(filename,".JPG") || strstr(filename,".jpeg") || strstr(filename,".JPEG"))
				fn_key='p';
			else 
				fn_key='o';
		}	
		// preview file name
		else if (strcmp(kv[i].key,"preview")==0)
		{
			prev_filename = kv[i].value;
		}
		i++;
	}

	if (fn_key)
	{
		cmd = cmd_find_by_key(fn_key);
		if (cmd)
		{
			struct snapshot_arg sarg;
			void * tmp;

			info("HTTP snapshot request into: ");
			info(filename);info("\n");	

			sarg.filename = filename;
			sarg.fmt = fn_key;
			sarg.com2 = 0;

			if (prev_filename)
			{
				info("Preview request into: ");
				info(prev_filename);info("\n");	
				sarg.preview_fn = prev_filename;
				sarg.com2 = 1;
				
			}
			
			tmp = pf->arg;
			pf->arg = &sarg;
			
			err = cmd->h(pf,NULL);
			if (err)
			{
				free(kv);
				mem_deref(prm);
				pf->arg = tmp;
				return re_hprintf(pf,err_msg);
			}
			
			pf->arg = tmp;			
		}
		else
		{
			free(kv);
			mem_deref(prm);
			return re_hprintf(pf,err_msg);
		}
	}
	else
	{
		free(kv);
		mem_deref(prm);
		return re_hprintf(pf,err_msg);
	}

	strcpy(ok_msg,"%H <body>\n");
	strcat(ok_msg,&prm[3]);
	strcat(ok_msg," saved.\n</body>\n</html>\n");
	mem_deref(prm);
	return re_hprintf(pf,ok_msg,html_print_head, NULL);
	               
}

int pl_str3cmp(struct pl *dst,char *src)
{
	if (!dst || !src || !dst->p)
		return -1;
	if (strlen(src)<3 || dst->l<3)
		return -2;
	if (dst->p[0]==src[0] && dst->p[1]==src[1] && dst->p[2]==src[2])
		return 1;
	return 0;
}

static void http_req_handler(struct http_conn *conn,
			     const struct http_msg *msg, void *arg)
{
	(void)arg;
	
	if (1 == pl_str3cmp(&msg->prm,"?o="))
	{

		http_creply(conn, 200, "OK",
			    "text/html;charset=UTF-8",
			    "%H", html_save_img, msg);

	}
	else if (0 == pl_strcasecmp(&msg->path, "/")) 
	{
		http_creply(conn, 200, "OK",
			    "text/html;charset=UTF-8",
			    "%H", html_print_cmd, msg);
	}
	else
	{
		http_ereply(conn, 404, "Not Found");
	}
}

static int module_init(void)
{
	struct sa laddr;
	int err;

	if (conf_get_sa(conf_cur(), "http_listen", &laddr)) {
		sa_set_str(&laddr, "0.0.0.0", 8000);
	}

	err = http_listen(&httpsock, &laddr, http_req_handler, NULL);
	if (err)
		return err;

	info("httpd: listening on %J\n", &laddr);

	return 0;
}


static int module_close(void)
{
	httpsock = mem_deref(httpsock);

	return 0;
}

#define MX_SPLIT 1024

static char **split( char **result, char *working, const char *src, const char *delim)
{
	int i;

	strcpy(working, src);
	char *p=strtok(working, delim);
	for(i=0; p!=NULL && i < (MX_SPLIT -1); i++, p=strtok(NULL, delim) )
	{
		result[i]=p;
		result[i+1]=NULL;
	}
	return result;
}
	
static struct key_value * parse_prm(char *prm)
{
	int i=0;

	char *result[MX_SPLIT]={NULL};
	char *working=malloc(MX_SPLIT);
	memset(working,0,MX_SPLIT);
	
	char *tresult[4];
	char *tworking=malloc(MX_SPLIT/4);
	memset(tworking,0,MX_SPLIT/4);

	char mydelim[]="?&";

	struct key_value * rez = calloc (16, sizeof(struct key_value));
	
	split(result, working, prm, mydelim);

	while(result[i]!=NULL)
	{
		if (strrchr(result[i],'='))
		{
			split(tresult,tworking, result[i], "=");
			rez[i].key = malloc(strlen(tresult[0])+1);
			strcpy(rez[i].key,tresult[0]);
			rez[i].value = malloc(strlen(tresult[1])+1);
			strcpy(rez[i].value,tresult[1]);
		}
		i++;
	}
	rez[i].key = NULL;
	rez[i].value = NULL;

	free(working);
	free(tworking);
	
	return rez;

}


EXPORT_SYM const struct mod_export DECL_EXPORTS(httpd) = {
	"httpd",
	"application",
	module_init,
	module_close,
};
