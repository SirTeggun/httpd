/* ====================================================================
 * The Apache Software License, Version 1.1
 *
 * Copyright (c) 2000 The Apache Software Foundation.  All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. The end-user documentation included with the redistribution,
 *    if any, must include the following acknowledgment:
 *       "This product includes software developed by the
 *        Apache Software Foundation (http://www.apache.org/)."
 *    Alternately, this acknowledgment may appear in the software itself,
 *    if and wherever such third-party acknowledgments normally appear.
 *
 * 4. The names "Apache" and "Apache Software Foundation" must
 *    not be used to endorse or promote products derived from this
 *    software without prior written permission. For written
 *    permission, please contact apache@apache.org.
 *
 * 5. Products derived from this software may not be called "Apache",
 *    nor may "Apache" appear in their name, without prior written
 *    permission of the Apache Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE APACHE SOFTWARE FOUNDATION OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * ====================================================================
 *
 * This software consists of voluntary contributions made by many
 * individuals on behalf of the Apache Software Foundation.  For more
 * information on the Apache Software Foundation, please see
 * <http://www.apache.org/>.
 *
 * Portions of this software are based upon public domain software
 * originally written at the National Center for Supercomputing Applications,
 * University of Illinois, Urbana-Champaign.
 */

/*
 * http_auth: authentication
 * 
 * Rob McCool & Brian Behlendorf.
 * 
 * Adapted to Apache by rst.
 *
 * Version 0.5 May 1996
 *
 * Modified by Dirk.vanGulik@jrc.it to
 * 
 * Adapted to allow anonymous logins, just like with Anon-FTP, when
 * one gives the magic user name 'anonymous' and ones email address
 * as the password.
 *
 * Just add the following tokes to your <directory> setup:
 * 
 * Anonymous                    magic-userid [magic-userid]...
 *
 * Anonymous_MustGiveEmail      [ on | off ] default = on
 * Anonymous_LogEmail           [ on | off ] default = on
 * Anonymous_VerifyEmail        [ on | off ] default = off
 * Anonymous_NoUserId           [ on | off ] default = off
 * Anonymous_Authoritative      [ on | off ] default = off
 *
 * The magic user id is something like 'anonymous', it is NOT case sensitive. 
 * 
 * The MustGiveEmail flag can be used to force users to enter something
 * in the password field (like an email address). Default is on.
 *
 * Furthermore the 'NoUserID' flag can be set to allow completely empty
 * usernames in as well; this can be is convenient as a single return
 * in broken GUIs like W95 is often given by the user. The Default is off.
 *
 * Dirk.vanGulik@jrc.it; http://ewse.ceo.org; http://me-www.jrc.it/~dirkx
 * 
 */

#include "httpd.h"
#include "http_config.h"
#include "http_core.h"
#include "http_log.h"
#include "http_protocol.h"
#include "http_request.h"
#include "apr_strings.h"

typedef struct anon_auth {
    char *password;
    struct anon_auth *next;
} anon_auth;

typedef struct {

    anon_auth *anon_auth_passwords;
    int anon_auth_nouserid;
    int anon_auth_logemail;
    int anon_auth_verifyemail;
    int anon_auth_mustemail;
    int anon_auth_authoritative;

} anon_auth_config_rec;

static void *create_anon_auth_dir_config(apr_pool_t *p, char *d)
{
    anon_auth_config_rec *sec = (anon_auth_config_rec *)
    apr_pcalloc(p, sizeof(anon_auth_config_rec));

    if (!sec)
	return NULL;		/* no memory... */

    /* just to illustrate the defaults really. */
    sec->anon_auth_passwords = NULL;

    sec->anon_auth_nouserid = 0;
    sec->anon_auth_logemail = 1;
    sec->anon_auth_verifyemail = 0;
    sec->anon_auth_mustemail = 1;
    sec->anon_auth_authoritative = 0;
    return sec;
}

static const char *anon_set_passwd_flag(cmd_parms *cmd,
				 void *dummy, int arg)
{
    anon_auth_config_rec *sec = dummy;
    sec->anon_auth_mustemail = arg;
    return NULL;
}

static const char *anon_set_userid_flag(cmd_parms *cmd,
				 void *dummy, int arg)
{
    anon_auth_config_rec *sec = dummy;
    sec->anon_auth_nouserid = arg;
    return NULL;
}

static const char *anon_set_logemail_flag(cmd_parms *cmd,
				   void *dummy, int arg)
{
    anon_auth_config_rec *sec = dummy;
    sec->anon_auth_logemail = arg;
    return NULL;
}

static const char *anon_set_verifyemail_flag(cmd_parms *cmd,
				      void *dummy, int arg)
{
    anon_auth_config_rec *sec = dummy;
    sec->anon_auth_verifyemail = arg;
    return NULL;
}
static const char *anon_set_authoritative_flag(cmd_parms *cmd,
					void *dummy, int arg)
{
    anon_auth_config_rec *sec = dummy;
    sec->anon_auth_authoritative = arg;
    return NULL;
}

static const char *anon_set_string_slots(cmd_parms *cmd,
				  void *dummy, const char *arg)
{
    anon_auth_config_rec *sec = dummy;
    anon_auth *first;

    if (!(*arg))
	return "Anonymous string cannot be empty, use Anonymous_NoUserId instead";

    /* squeeze in a record */
    first = sec->anon_auth_passwords;

    if (!(sec->anon_auth_passwords = apr_palloc(cmd->pool, sizeof(anon_auth))) ||
       !(sec->anon_auth_passwords->password = apr_pstrdup(cmd->pool, arg)))
	     return "Failed to claim memory for an anonymous password...";

    /* and repair the next */
    sec->anon_auth_passwords->next = first;

    return NULL;
}

static const command_rec anon_auth_cmds[] =
{
    AP_INIT_ITERATE("Anonymous", anon_set_string_slots, NULL, OR_AUTHCFG, 
     "a space-separated list of user IDs"),
    AP_INIT_FLAG("Anonymous_MustGiveEmail", anon_set_passwd_flag, NULL, 
     OR_AUTHCFG, "Limited to 'on' or 'off'"),
    AP_INIT_FLAG("Anonymous_NoUserId", anon_set_userid_flag, NULL, OR_AUTHCFG, 
     "Limited to 'on' or 'off'"),
    AP_INIT_FLAG("Anonymous_VerifyEmail", anon_set_verifyemail_flag, NULL, 
     OR_AUTHCFG, "Limited to 'on' or 'off'"),
    AP_INIT_FLAG("Anonymous_LogEmail", anon_set_logemail_flag, NULL, OR_AUTHCFG,
     "Limited to 'on' or 'off'"),
    AP_INIT_FLAG("Anonymous_Authoritative", anon_set_authoritative_flag, NULL, 
     OR_AUTHCFG, "Limited to 'on' or 'off'"),
    {NULL}
};

module AP_MODULE_DECLARE_DATA anon_auth_module;

static int anon_authenticate_basic_user(request_rec *r)
{
    anon_auth_config_rec *sec =
    (anon_auth_config_rec *) ap_get_module_config(r->per_dir_config,
					       &anon_auth_module);
    const char *sent_pw;
    int res = DECLINED;

    if ((res = ap_get_basic_auth_pw(r, &sent_pw)))
	return res;

    /* Ignore if we are not configured */
    if (!sec->anon_auth_passwords)
	return DECLINED;

    /* Do we allow an empty userID and/or is it the magic one
     */

    if ((!(r->user[0])) && (sec->anon_auth_nouserid)) {
	res = OK;
    }
    else {
	anon_auth *p = sec->anon_auth_passwords;
	res = DECLINED;
	while ((res == DECLINED) && (p != NULL)) {
	    if (!(strcasecmp(r->user, p->password)))
		res = OK;
	    p = p->next;
	}
    }
    if (
    /* username is OK */
	   (res == OK)
    /* password been filled out ? */
	   && ((!sec->anon_auth_mustemail) || strlen(sent_pw))
    /* does the password look like an email address ? */
	   && ((!sec->anon_auth_verifyemail)
	       || ((strpbrk("@", sent_pw) != NULL)
		   && (strpbrk(".", sent_pw) != NULL)))) {
	if (sec->anon_auth_logemail && ap_is_initial_req(r)) {
	    ap_log_rerror(APLOG_MARK, APLOG_NOERRNO|APLOG_INFO, APR_SUCCESS, r,
			"Anonymous: Passwd <%s> Accepted",
			sent_pw ? sent_pw : "\'none\'");
	}
	return OK;
    }
    else {
	if (sec->anon_auth_authoritative) {
	    ap_log_rerror(APLOG_MARK, APLOG_NOERRNO|APLOG_ERR, APR_SUCCESS, r,
			"Anonymous: Authoritative, Passwd <%s> not accepted",
			sent_pw ? sent_pw : "\'none\'");
	    return HTTP_UNAUTHORIZED;
	}
	/* Drop out the bottom to return DECLINED */
    }

    return DECLINED;
}

static int check_anon_access(request_rec *r)
{
#ifdef NOTYET
    conn_rec *c = r->connection;
    anon_auth_config_rec *sec =
    (anon_auth_config_rec *) ap_get_module_config(r->per_dir_config,
					       &anon_auth_module);

    if (!sec->anon_auth)
	return DECLINED;

    if (strcasecmp(r->connection->user, sec->anon_auth))
	return DECLINED;

    return OK;
#endif
    return DECLINED;
}

static void register_hooks(apr_pool_t *p)
{
    ap_hook_check_user_id(anon_authenticate_basic_user,NULL,NULL,APR_HOOK_MIDDLE);
    ap_hook_auth_checker(check_anon_access,NULL,NULL,APR_HOOK_MIDDLE);
}

module AP_MODULE_DECLARE_DATA anon_auth_module =
{
    STANDARD20_MODULE_STUFF,
    create_anon_auth_dir_config,/* dir config creater */
    NULL,			/* dir merger ensure strictness */
    NULL,			/* server config */
    NULL,			/* merge server config */
    anon_auth_cmds,		/* command apr_table_t */
    register_hooks		/* register hooks */
};
