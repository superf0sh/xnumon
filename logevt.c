/*-
 * xnumon - monitor macOS for malicious activity
 * https://www.roe.ch/xnumon
 *
 * Copyright (c) 2017-2019, Daniel Roethlisberger <daniel@roe.ch>.
 * All rights reserved.
 *
 * Licensed under the Open Software License version 3.0.
 */

/*
 * This compilation unit contains the code that drives logging through the
 * configured log format engine.  The structure and content of the logged data
 * is decided here.
 *
 * Currently, this code also does runtime translation of user, group etc IDs
 * into names.  The reason for this is that we do not want to block the worker
 * thread with such lookups, because they are not as time-critical as the
 * acquisition of hashes and code signatures.  As a side benefit, we do not
 * need to malloc space for the strings as we can print them to the log record
 * right away.
 *
 * General design decisions:
 * - only use null values for configuration, not for data
 */

#include "logevt.h"

#include "build.h"
#include "os.h"
#include "evtloop.h"
#include "procmon.h"
#include "filemon.h"
#include "hackmon.h"
#include "sockmon.h"
#include "str.h"
#include "sys.h"

#include <assert.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>

static config_t *config;

void
logevt_init(config_t *cfg) {
	config = cfg;
}

static void
logevt_uid(logfmt_t *fmt, FILE *f,
           uid_t uid, const char *idlabel, const char *namelabel) {
	struct passwd *pw;

	fmt->dict_item(f, idlabel);
	if (uid == (uid_t)-1) {
		fmt->value_int(f, -1);
		return;
	}
	fmt->value_uint(f, uid);

	if (config->resolve_users_groups) {
		pw = getpwuid(uid);
		if (pw) {
			fmt->dict_item(f, namelabel);
			fmt->value_string(f, pw->pw_name);
		}
	}
}

static void
logevt_gid(logfmt_t *fmt, FILE *f,
           gid_t gid, const char *idlabel, const char *namelabel) {
	struct group *gr;

	fmt->dict_item(f, idlabel);
	if (gid == (gid_t)-1) {
		fmt->value_int(f, -1);
		return;
	}
	fmt->value_uint(f, gid);

	if (config->resolve_users_groups) {
		gr = getgrgid(gid);
		if (gr) {
			fmt->dict_item(f, namelabel);
			fmt->value_string(f, gr->gr_name);
		}
	}
}

static void
logevt_header(logfmt_t *fmt, FILE *f, logevt_header_t *hdr) {
	assert(hdr);
	fmt->record_begin(f);
	fmt->dict_begin(f);
	fmt->dict_item(f, "version");
	fmt->value_uint(f, LOGEVT_VERSION);
	fmt->dict_item(f, "time");
	fmt->value_timespec(f, &hdr->tv);
	fmt->dict_item(f, "eventcode");
	fmt->value_uint(f, hdr->code);
}

static void
logevt_footer(logfmt_t *fmt, FILE *f) {
	fmt->dict_end(f);
	fmt->record_end(f);
}

int
logevt_xnumon_ops(logfmt_t *fmt, FILE *f, void *arg0) {
	xnumon_ops_t *ops = (xnumon_ops_t *)arg0;

	logevt_header(fmt, f, (logevt_header_t *)arg0);

	fmt->dict_item(f, "op");
	fmt->value_string(f, ops->subtype);

	fmt->dict_item(f, "build");
	fmt->dict_begin(f);
	fmt->dict_item(f, "version");
	fmt->value_string(f, build_version);
	fmt->dict_item(f, "date");
	fmt->value_string(f, build_date);
	fmt->dict_item(f, "info");
	fmt->value_string(f, build_info);
	fmt->dict_end(f); /* build */

	fmt->dict_item(f, "config");
	fmt->dict_begin(f);
	fmt->dict_item(f, "path");
	fmt->value_string(f, config->path);
	fmt->dict_item(f, "id");
	if (config->id)
		fmt->value_string(f, config->id);
	else
		fmt->value_null(f);
	fmt->dict_item(f, "launchd_mode");
	fmt->value_bool(f, config->launchd_mode);
	fmt->dict_item(f, "debug");
	fmt->value_bool(f, config->debug);
	fmt->dict_item(f, "events");
	char *evts = config_events_s(config);
	fmt->value_string(f, evts);
	free(evts);
	fmt->dict_item(f, "stats_interval");
	fmt->value_uint(f, config->stats_interval);
	fmt->dict_item(f, "kextlevel");
	fmt->value_string(f, config_kextlevel_s(config));
	fmt->dict_item(f, "hashes");
	fmt->value_string(f, hashes_flags_s(config->hflags));
	fmt->dict_item(f, "codesign");
	fmt->value_bool(f, config->codesign);
	fmt->dict_item(f, "envlevel");
	fmt->value_string(f, config_envlevel_s(config));
	fmt->dict_item(f, "resolve_users_groups");
	fmt->value_bool(f, config->resolve_users_groups);
	fmt->dict_item(f, "omit_mode");
	fmt->value_bool(f, config->omit_mode);
	fmt->dict_item(f, "omit_size");
	fmt->value_bool(f, config->omit_size);
	fmt->dict_item(f, "omit_mtime");
	fmt->value_bool(f, config->omit_mtime);
	fmt->dict_item(f, "omit_ctime");
	fmt->value_bool(f, config->omit_ctime);
	fmt->dict_item(f, "omit_btime");
	fmt->value_bool(f, config->omit_btime);
	fmt->dict_item(f, "omit_sid");
	fmt->value_bool(f, config->omit_sid);
	fmt->dict_item(f, "omit_groups");
	fmt->value_bool(f, config->omit_groups);
	fmt->dict_item(f, "omit_apple_hashes");
	fmt->value_bool(f, config->omit_apple_hashes);
	fmt->dict_item(f, "ancestors");
	if (config->ancestors < SIZE_MAX)
		fmt->value_uint(f, config->ancestors);
	else
		fmt->value_string(f, "unlimited");
	fmt->dict_item(f, "logdst");
	fmt->value_string(f, logdst_s(config));
	fmt->dict_item(f, "logfmt");
	fmt->value_string(f, logfmt_s(config));
	fmt->dict_item(f, "logoneline");
	if (config->logoneline == -1)
		fmt->value_null(f);
	else
		fmt->value_bool(f, config->logoneline);
	fmt->dict_item(f, "logfile");
	if (config->logfile)
		fmt->value_string(f, config->logfile);
	else
		fmt->value_null(f);
	fmt->dict_item(f, "limit_nofile");
	fmt->value_uint(f, config->limit_nofile);
	fmt->dict_item(f, "suppress_image_exec_at_start");
	fmt->value_bool(f, config->suppress_image_exec_at_start);
	fmt->dict_item(f, "suppress_image_exec_by_ident");
	fmt->value_uint(f, setstr_size(&config->suppress_image_exec_by_ident));
	fmt->dict_item(f, "suppress_image_exec_by_path");
	fmt->value_uint(f, setstr_size(&config->suppress_image_exec_by_path));
	fmt->dict_item(f, "suppress_image_exec_by_ancestor_ident");
	fmt->value_uint(f,
		setstr_size(&config->suppress_image_exec_by_ancestor_ident));
	fmt->dict_item(f, "suppress_image_exec_by_ancestor_path");
	fmt->value_uint(f,
		setstr_size(&config->suppress_image_exec_by_ancestor_path));
	fmt->dict_item(f, "suppress_process_access_by_subject_ident");
	fmt->value_uint(f,
		setstr_size(&config->suppress_process_access_by_subject_ident));
	fmt->dict_item(f, "suppress_process_access_by_subject_path");
	fmt->value_uint(f,
		setstr_size(&config->suppress_process_access_by_subject_path));
	fmt->dict_item(f, "suppress_socket_op_localhost");
	fmt->value_bool(f, config->suppress_socket_op_localhost);
	fmt->dict_item(f, "suppress_socket_op_by_subject_ident");
	fmt->value_uint(f,
		setstr_size(&config->suppress_socket_op_by_subject_ident));
	fmt->dict_item(f, "suppress_socket_op_by_subject_path");
	fmt->value_uint(f,
		setstr_size(&config->suppress_socket_op_by_subject_path));
	fmt->dict_end(f); /* config */

	fmt->dict_item(f, "system");
	fmt->dict_begin(f);
	fmt->dict_item(f, "name");
	fmt->value_string(f, os_name());
	fmt->dict_item(f, "version");
	fmt->value_string(f, os_version());
	fmt->dict_item(f, "build");
	fmt->value_string(f, os_build());
	fmt->dict_end(f); /* system */

	logevt_footer(fmt, f);
	return 0;
}

int
logevt_xnumon_stats(logfmt_t *fmt, FILE *f, void *arg0) {
	evtloop_stat_t *st = (evtloop_stat_t *)arg0;

	logevt_header(fmt, f, (logevt_header_t *)arg0);

	fmt->dict_item(f, "evtloop");
	fmt->dict_begin(f);
	fmt->dict_item(f, "aupclobber");
	fmt->value_uint(f, st->el_aupclobbers);
	fmt->dict_item(f, "aueunknown");
	fmt->value_uint(f, st->el_aueunknowns);
	fmt->dict_item(f, "failedsyscall");
	fmt->value_uint(f, st->el_failedsyscalls);
	fmt->dict_item(f, "radar38845422");
	fmt->value_uint(f, st->el_radar38845422);
	fmt->dict_item(f, "radar38845422_fatal");
	fmt->value_uint(f, st->el_radar38845422_fatal);
	fmt->dict_item(f, "radar38845784");
	fmt->value_uint(f, st->el_radar38845784);
	fmt->dict_item(f, "radar39267328");
	fmt->value_uint(f, st->el_radar39267328);
	fmt->dict_item(f, "radar39267328_fatal");
	fmt->value_uint(f, st->el_radar39267328_fatal);
	fmt->dict_item(f, "radar39623812");
	fmt->value_uint(f, st->el_radar39623812);
	fmt->dict_item(f, "radar39623812_fatal");
	fmt->value_uint(f, st->el_radar39623812_fatal);
	fmt->dict_item(f, "radar42770257");
	fmt->value_uint(f, st->el_radar42770257);
	fmt->dict_item(f, "radar42770257_fatal");
	fmt->value_uint(f, st->el_radar42770257_fatal);
	fmt->dict_item(f, "radar42783724");
	fmt->value_uint(f, st->el_radar42783724);
	fmt->dict_item(f, "radar42783724_fatal");
	fmt->value_uint(f, st->el_radar42783724_fatal);
	fmt->dict_item(f, "radar42784847");
	fmt->value_uint(f, st->el_radar42784847);
	fmt->dict_item(f, "radar42784847_fatal");
	fmt->value_uint(f, st->el_radar42784847_fatal);
	fmt->dict_item(f, "radar42946744");
	fmt->value_uint(f, st->el_radar42946744);
	fmt->dict_item(f, "radar42946744_fatal");
	fmt->value_uint(f, st->el_radar42946744_fatal);
	fmt->dict_item(f, "radar43151662");
	fmt->value_uint(f, st->el_radar43151662);
	fmt->dict_item(f, "radar43151662_fatal");
	fmt->value_uint(f, st->el_radar43151662_fatal);
	fmt->dict_item(f, "missingtoken");
	fmt->value_uint(f, st->el_missingtoken);
	fmt->dict_item(f, "oom");
	fmt->value_uint(f, st->el_ooms);
	fmt->dict_end(f); /* evtloop */

	fmt->dict_item(f, "procmon");
	fmt->dict_begin(f);
	fmt->dict_item(f, "actprocs");
	fmt->value_uint(f, st->pm.procs);
	fmt->dict_item(f, "actexecimages");
	fmt->value_uint(f, st->pm.images);
	fmt->dict_item(f, "liveacq");
	fmt->value_uint(f, st->pm.liveacq);
	fmt->dict_item(f, "miss");
	fmt->dict_begin(f);
	fmt->dict_item(f, "bypid");
	fmt->value_uint(f, st->pm.miss_bypid);
	fmt->dict_item(f, "forksubj");
	fmt->value_uint(f, st->pm.miss_forksubj);
	fmt->dict_item(f, "execsubj");
	fmt->value_uint(f, st->pm.miss_execsubj);
	fmt->dict_item(f, "execinterp");
	fmt->value_uint(f, st->pm.miss_execinterp);
	fmt->dict_item(f, "chdirsubj");
	fmt->value_uint(f, st->pm.miss_chdirsubj);
	fmt->dict_item(f, "getcwd");
	fmt->value_uint(f, st->pm.miss_getcwd);
	fmt->dict_end(f); /* miss */
	fmt->dict_item(f, "oom");
	fmt->value_uint(f, st->pm.ooms);
	fmt->dict_end(f); /* procmon */

	fmt->dict_item(f, "hackmon");
	fmt->dict_begin(f);
	fmt->dict_item(f, "recvd");
	fmt->value_uint(f, st->hm.recvd);
	fmt->dict_item(f, "procd");
	fmt->value_uint(f, st->hm.procd);
	fmt->dict_item(f, "oom");
	fmt->value_uint(f, st->hm.ooms);
	fmt->dict_end(f); /* hackmon */

	fmt->dict_item(f, "filemon");
	fmt->dict_begin(f);
	fmt->dict_item(f, "recvd");
	fmt->value_uint(f, st->fm.recvd);
	fmt->dict_item(f, "procd");
	fmt->value_uint(f, st->fm.procd);
	fmt->dict_item(f, "lpmiss");
	fmt->value_uint(f, st->fm.lpmiss);
	fmt->dict_item(f, "oom");
	fmt->value_uint(f, st->fm.ooms);
	fmt->dict_end(f); /* filemon */

	fmt->dict_item(f, "sockmon");
	fmt->dict_begin(f);
	fmt->dict_item(f, "recvd");
	fmt->value_uint(f, st->sm.recvd);
	fmt->dict_item(f, "procd");
	fmt->value_uint(f, st->sm.procd);
	fmt->dict_item(f, "oom");
	fmt->value_uint(f, st->sm.ooms);
	fmt->dict_end(f); /* sockmon */

	fmt->dict_item(f, "kext_cdevq");
	fmt->dict_begin(f);
	fmt->dict_item(f, "buckets");
	fmt->value_uint(f, st->ke.cdev_qsize);
	fmt->dict_item(f, "visitors");
	fmt->value_uint(f, st->ke.kauth_visitors);
	fmt->dict_item(f, "timeout");
	fmt->value_uint(f, st->ke.kauth_timeouts);
	fmt->dict_item(f, "error");
	fmt->value_uint(f, st->ke.kauth_errors);
	fmt->dict_item(f, "defer");
	fmt->value_uint(f, st->ke.kauth_defers);
	fmt->dict_item(f, "deny");
	fmt->value_uint(f, st->ke.kauth_denies);
	fmt->dict_end(f); /* kext-cdevq */

	fmt->dict_item(f, "prep_queue");
	fmt->dict_begin(f);
	fmt->dict_item(f, "buckets");
	fmt->value_uint(f, st->pm.pqsize);
	fmt->dict_item(f, "lookup");
	fmt->value_uint(f, st->pm.pqlookup);
	fmt->dict_item(f, "miss");
	fmt->value_uint(f, st->pm.pqmiss);
	fmt->dict_item(f, "drop");
	fmt->value_uint(f, st->pm.pqdrop);
	fmt->dict_item(f, "bktskip");
	fmt->value_uint(f, st->pm.pqskip);
	fmt->dict_end(f); /* prep-queue */

	fmt->dict_item(f, "aupi_cdevq");
	fmt->dict_begin(f);
	fmt->dict_item(f, "buckets");
	fmt->value_uint(f, st->ap.qlen);
	fmt->dict_item(f, "bucketmax");
	fmt->value_uint(f, st->ap.qlimit);
	fmt->dict_item(f, "insert");
	fmt->value_uint(f, st->ap.inserts);
	fmt->dict_item(f, "read");
	fmt->value_uint(f, st->ap.reads);
	fmt->dict_item(f, "drop");
	fmt->value_uint(f, st->ap.drops);
	fmt->dict_end(f); /* aupi-cdevq */

	fmt->dict_item(f, "work_queue");
	fmt->dict_begin(f);
	fmt->dict_item(f, "buckets");
	fmt->value_uint(f, st->wq.qsize);
	fmt->dict_end(f); /* work-queue */

	fmt->dict_item(f, "log_queue");
	fmt->dict_begin(f);
	fmt->dict_item(f, "buckets");
	fmt->value_uint(f, st->lq.qsize);
	fmt->dict_item(f, "events");
	fmt->list_begin(f);
	for (int i = 0; i < LOGEVT_SIZE; i++) {
		fmt->list_item(f, "event");
		fmt->value_uint(f, st->lq.counts[i]);
	}
	fmt->list_end(f);
	fmt->dict_item(f, "errors");
	fmt->value_uint(f, st->lq.errors);
	fmt->dict_end(f); /* log-queue */

	fmt->dict_item(f, "hash_cache");
	fmt->dict_begin(f);
	fmt->dict_item(f, "buckets");
	fmt->value_uint(f, st->ch.used);
	fmt->dict_item(f, "bucketmax");
	fmt->value_uint(f, st->ch.size);
	fmt->dict_item(f, "put");
	fmt->value_uint(f, st->ch.puts);
	fmt->dict_item(f, "get");
	fmt->value_uint(f, st->ch.gets);
	fmt->dict_item(f, "hit");
	fmt->value_uint(f, st->ch.hits);
	fmt->dict_item(f, "miss");
	fmt->value_uint(f, st->ch.misses);
	fmt->dict_item(f, "inv");
	fmt->value_uint(f, st->ch.invalids);
	fmt->dict_end(f); /* hash-cache */

	fmt->dict_item(f, "csig_cache");
	fmt->dict_begin(f);
	fmt->dict_item(f, "buckets");
	fmt->value_uint(f, st->cc.used);
	fmt->dict_item(f, "bucketmax");
	fmt->value_uint(f, st->cc.size);
	fmt->dict_item(f, "put");
	fmt->value_uint(f, st->cc.puts);
	fmt->dict_item(f, "get");
	fmt->value_uint(f, st->cc.gets);
	fmt->dict_item(f, "hit");
	fmt->value_uint(f, st->cc.hits);
	fmt->dict_item(f, "miss");
	fmt->value_uint(f, st->cc.misses);
	fmt->dict_item(f, "inv");
	fmt->value_uint(f, st->cc.invalids);
	fmt->dict_end(f); /* csig-cache */

	fmt->dict_item(f, "ldpl_cache");
	fmt->dict_begin(f);
	fmt->dict_item(f, "buckets");
	fmt->value_uint(f, st->cl.used);
	fmt->dict_item(f, "bucketmax");
	fmt->value_uint(f, st->cl.size);
	fmt->dict_item(f, "put");
	fmt->value_uint(f, st->cl.puts);
	fmt->dict_item(f, "get");
	fmt->value_uint(f, st->cl.gets);
	fmt->dict_item(f, "hit");
	fmt->value_uint(f, st->cl.hits);
	fmt->dict_item(f, "miss");
	fmt->value_uint(f, st->cl.misses);
	fmt->dict_item(f, "inv");
	fmt->value_uint(f, st->cl.invalids);
	fmt->dict_end(f); /* ldpl-cache */

	logevt_footer(fmt, f);
	return 0;
}

static void
logevt_image_exec_image(logfmt_t *fmt, FILE *f, image_exec_t *ie) {
	fmt->dict_begin(f);
	fmt->dict_item(f, "path");
	fmt->value_string(f, ie->path);
	if (ie->flags & (EIFLAG_STAT|EIFLAG_ATTR)) {
		if (!config->omit_mode) {
			fmt->dict_item(f, "mode");
			fmt->value_uint_oct(f, ie->stat.mode);
		}
		logevt_uid(fmt, f, ie->stat.uid, "uid", "uname");
		if (!config->omit_groups) {
			logevt_gid(fmt, f, ie->stat.gid, "gid", "gname");
		}
	}
	if (ie->flags & EIFLAG_STAT) {
		if (!config->omit_size) {
			fmt->dict_item(f, "size");
			fmt->value_uint(f, ie->stat.size);
		}
		if (!config->omit_mtime) {
			fmt->dict_item(f, "mtime");
			fmt->value_timespec(f, &ie->stat.mtime);
		}
		if (!config->omit_ctime) {
			fmt->dict_item(f, "ctime");
			fmt->value_timespec(f, &ie->stat.ctime);
		}
		if (!config->omit_btime) {
			fmt->dict_item(f, "btime");
			fmt->value_timespec(f, &ie->stat.btime);
		}
	}
	if ((ie->flags & EIFLAG_HASHES) &&
	    (!config->omit_apple_hashes ||
	     !ie->codesign ||
	     !codesign_is_apple_system(ie->codesign))) {
		if (config->hflags & HASH_MD5) {
			fmt->dict_item(f, "md5");
			fmt->value_buf_hex(f, ie->hashes.md5, MD5SZ);
		}
		if (config->hflags & HASH_SHA1) {
			fmt->dict_item(f, "sha1");
			fmt->value_buf_hex(f, ie->hashes.sha1, SHA1SZ);
		}
		if (config->hflags & HASH_SHA256) {
			fmt->dict_item(f, "sha256");
			fmt->value_buf_hex(f, ie->hashes.sha256, SHA256SZ);
		}
	}

	if (ie->codesign) {
		fmt->dict_item(f, "signature");
		fmt->value_string(f, codesign_result_s(ie->codesign));
		if (ie->codesign->origin) {
			fmt->dict_item(f, "origin");
			fmt->value_string(f, codesign_origin_s(ie->codesign));
		}
		if (ie->codesign->cdhash) {
			fmt->dict_item(f, "cdhash");
			fmt->value_buf_hex(f, ie->codesign->cdhash,
			                      ie->codesign->cdhashsz);
		}
		if (ie->codesign->ident) {
			fmt->dict_item(f, "ident");
			fmt->value_string(f, ie->codesign->ident);
		}
		if (ie->codesign->teamid) {
			fmt->dict_item(f, "teamid");
			fmt->value_string(f, ie->codesign->teamid);
		}
		if (ie->codesign->certcn) {
			fmt->dict_item(f, "certcn");
			fmt->value_string(f, ie->codesign->certcn);
		}
	}
	fmt->dict_end(f); /* image */
}

static void
logevt_process_image_exec(logfmt_t *fmt, FILE *f, image_exec_t *ie) {
	fmt->dict_begin(f);
	if (!(ie->flags & EIFLAG_PIDLOOKUP)) {
		fmt->dict_item(f, "exec_time");
		fmt->value_timespec(f, &ie->hdr.tv);
	}
	fmt->dict_item(f, "exec_pid");
	fmt->value_int(f, ie->pid);
	fmt->dict_item(f, "path");
	fmt->value_string(f, ie->path);
	if ((ie->flags & EIFLAG_HASHES) &&
	    (!config->omit_apple_hashes ||
	     !ie->codesign ||
	     !codesign_is_apple_system(ie->codesign))) {
		if (config->hflags & HASH_MD5) {
			fmt->dict_item(f, "md5");
			fmt->value_buf_hex(f, ie->hashes.md5, MD5SZ);
		}
		if (config->hflags & HASH_SHA1) {
			fmt->dict_item(f, "sha1");
			fmt->value_buf_hex(f, ie->hashes.sha1, SHA1SZ);
		}
		if (config->hflags & HASH_SHA256) {
			fmt->dict_item(f, "sha256");
			fmt->value_buf_hex(f, ie->hashes.sha256, SHA256SZ);
		}
	}
	if (ie->codesign && codesign_is_good(ie->codesign)) {
		if (ie->codesign->ident) {
			fmt->dict_item(f, "ident");
			fmt->value_string(f, ie->codesign->ident);
		}
		if (ie->codesign->teamid) {
			fmt->dict_item(f, "teamid");
			fmt->value_string(f, ie->codesign->teamid);
		}
	}
	if (ie->script) {
		fmt->dict_item(f, "script");
		fmt->dict_begin(f);
		fmt->dict_item(f, "path");
		fmt->value_string(f, ie->script->path);
		assert(!ie->script->codesign);
		if (ie->script->flags & EIFLAG_HASHES) {
			if (config->hflags & HASH_MD5) {
				fmt->dict_item(f, "md5");
				fmt->value_buf_hex(f,
				        ie->script->hashes.md5, MD5SZ);
			}
			if (config->hflags & HASH_SHA1) {
				fmt->dict_item(f, "sha1");
				fmt->value_buf_hex(f,
				        ie->script->hashes.sha1, SHA1SZ);
			}
			if (config->hflags & HASH_SHA256) {
				fmt->dict_item(f, "sha256");
				fmt->value_buf_hex(f,
				        ie->script->hashes.sha256, SHA256SZ);
			}
		}
		fmt->dict_end(f); /* script */
	}
	fmt->dict_end(f); /* exec */
}

static void
logevt_process_image_exec_ancestors(logfmt_t *fmt, FILE *f, image_exec_t *ie) {
	size_t depth = 0;

	fmt->list_begin(f);
	for (image_exec_t *pie = ie; pie && pie->pid > 0; pie = pie->prev) {
		if (depth == config->ancestors)
			break;
		fmt->list_item(f, "ancestor");
		logevt_process_image_exec(fmt, f, pie);
		depth++;
	}
	fmt->list_end(f); /* process image exec ancestors */
}

/*
 * If we only know the pid and not the full process information, processpid
 * is != 0 and process must be ignored even if it is != NULL.
 */
static void
logevt_process(logfmt_t *fmt, FILE *f,
               audit_proc_t *process, pid_t processpid,
               image_exec_t *ie) {
	fmt->dict_begin(f);
	if (ie && (ie->flags & EIFLAG_PIDLOOKUP)) {
		fmt->dict_item(f, "reconstructed");
		fmt->value_bool(f, true);
	}
	if (processpid > 0) {
		fmt->dict_item(f, "pid");
		fmt->value_int(f, processpid);
	} else if (process) {
		fmt->dict_item(f, "pid");
		fmt->value_int(f, process->pid);
		logevt_uid(fmt, f, process->auid, "auid", "auname");
		logevt_uid(fmt, f, process->euid, "euid", "euname");
		if (!config->omit_groups) {
			logevt_gid(fmt, f, process->egid, "egid", "egname");
		}
		logevt_uid(fmt, f, process->ruid, "ruid", "runame");
		if (!config->omit_groups) {
			logevt_gid(fmt, f, process->rgid, "rgid", "rgname");
		}
		if (!config->omit_sid) {
			fmt->dict_item(f, "sid");
			fmt->value_uint(f, process->sid);
		}
		if (process->dev != (dev_t)-1) {
			fmt->dict_item(f, "dev");
			fmt->value_ttydev(f, process->dev);
		}
		if (!ipaddr_is_empty(&process->addr)) {
			fmt->dict_item(f, "addr");
			fmt->value_string(f, ipaddrtoa(&process->addr, NULL));
		}
	}
	if (ie) {
		if (ie->fork_tv.tv_sec > 0) {
			fmt->dict_item(f, "fork_time");
			fmt->value_timespec(f, &ie->fork_tv);
		}
		fmt->dict_item(f, "image");
		logevt_process_image_exec(fmt, f, ie);
		if (config->ancestors > 0) {
			fmt->dict_item(f, "ancestors");
			logevt_process_image_exec_ancestors(fmt, f, ie->prev);
		}
	}
	fmt->dict_end(f); /* process */
}

int
logevt_image_exec(logfmt_t *fmt, FILE *f, void *arg0) {
	image_exec_t *ie = (image_exec_t *)arg0;

	logevt_header(fmt, f, (logevt_header_t *)arg0);

	if (ie->flags & EIFLAG_PIDLOOKUP) {
		fmt->dict_item(f, "reconstructed");
		fmt->value_bool(f, true);
	}

	if (ie->argv) {
		fmt->dict_item(f, "argv");
		fmt->list_begin(f);
		for (int i = 0; ie->argv[i]; i++) {
			fmt->list_item(f, "arg");
			fmt->value_string(f, ie->argv[i]);
		}
		fmt->list_end(f); /* argv */
	}

	if (ie->envv) {
		fmt->dict_item(f, "env");
		fmt->list_begin(f);
		for (int i = 0; ie->envv[i]; i++) {
			fmt->list_item(f, "var");
			fmt->value_string(f, ie->envv[i]);
		}
		fmt->list_end(f); /* env */
	}

	if (ie->cwd) {
		fmt->dict_item(f, "cwd");
		fmt->value_string(f, ie->cwd);
	}

	fmt->dict_item(f, "image");
	logevt_image_exec_image(fmt, f, ie);

	if (ie->script) {
		fmt->dict_item(f, "script");
		logevt_image_exec_image(fmt, f, ie->script);
	}

	fmt->dict_item(f, "subject");
	logevt_process(fmt, f,
	               (ie->flags & EIFLAG_PIDLOOKUP) ? NULL : &ie->subject, 0,
	               ie->prev);

	logevt_footer(fmt, f);
	return 0;
}

int
logevt_process_access(logfmt_t *fmt, FILE *f, void *arg0) {
	process_access_t *pa = (process_access_t *)arg0;

	logevt_header(fmt, f, (logevt_header_t *)arg0);

	fmt->dict_item(f, "method");
	fmt->value_string(f, pa->method);

	fmt->dict_item(f, "object");
	logevt_process(fmt, f,
	               &pa->object, pa->objectpid,
	               pa->object_image_exec);

	fmt->dict_item(f, "subject");
	logevt_process(fmt, f,
	               &pa->subject, 0,
	               pa->subject_image_exec);

	logevt_footer(fmt, f);
	return 0;
}

int
logevt_launchd_add(logfmt_t *fmt, FILE *f, void *arg0) {
	launchd_add_t *ldadd = (launchd_add_t *)arg0;

	logevt_header(fmt, f, (logevt_header_t *)arg0);

	fmt->dict_item(f, "plist");
	fmt->dict_begin(f);
	fmt->dict_item(f, "path");
	fmt->value_string(f, ldadd->plist_path);
	fmt->dict_end(f); /* plist */

	fmt->dict_item(f, "program");
	fmt->dict_begin(f);
	if (ldadd->program_rpath) {
		fmt->dict_item(f, "rpath");
		fmt->value_string(f, ldadd->program_rpath);
	}
	if (ldadd->program_path) {
		fmt->dict_item(f, "path");
		fmt->value_string(f, ldadd->program_path);
	}
	if (ldadd->program_argv) {
		fmt->dict_item(f, "argv");
		fmt->list_begin(f);
		for (size_t i = 0; ldadd->program_argv[i]; i++) {
			fmt->list_item(f, "arg");
			fmt->value_string(f, ldadd->program_argv[i]);
		}
		fmt->list_end(f); /* argv */
	}
	fmt->dict_end(f); /* program */

	if (!(ldadd->flags & LAFLAG_NOSUBJECT)) {
		fmt->dict_item(f, "subject");
		logevt_process(fmt, f,
		               &ldadd->subject, 0,
		               ldadd->subject_image_exec);
	}

	logevt_footer(fmt, f);
	return 0;
}

int
logevt_socket_listen(logfmt_t *fmt, FILE *f, void *arg0) {
	socket_listen_t *so = (socket_listen_t *)arg0;

	logevt_header(fmt, f, (logevt_header_t *)arg0);

	if (so->protocol) {
		fmt->dict_item(f, "proto");
		fmt->value_string(f, protocoltoa(so->protocol));
	}

	if (!ipaddr_is_empty(&so->sock_addr)) {
		fmt->dict_item(f, "sockaddr");
		fmt->value_string(f, ipaddrtoa(&so->sock_addr, NULL));
		fmt->dict_item(f, "sockport");
		fmt->value_uint(f, so->sock_port);
	}

	fmt->dict_item(f, "subject");
	logevt_process(fmt, f,
	               &so->subject, 0,
	               so->subject_image_exec);

	logevt_footer(fmt, f);
	return 0;
}

int
logevt_socket_accept(logfmt_t *fmt, FILE *f, void *arg0) {
	socket_accept_t *so = (socket_accept_t *)arg0;

	logevt_header(fmt, f, (logevt_header_t *)arg0);

	if (so->protocol) {
		fmt->dict_item(f, "proto");
		fmt->value_string(f, protocoltoa(so->protocol));
	}

	if (!ipaddr_is_empty(&so->sock_addr)) {
		fmt->dict_item(f, "sockaddr");
		fmt->value_string(f, ipaddrtoa(&so->sock_addr, NULL));
		fmt->dict_item(f, "sockport");
		fmt->value_uint(f, so->sock_port);
	}

	if (!ipaddr_is_empty(&so->peer_addr)) {
		fmt->dict_item(f, "peeraddr");
		fmt->value_string(f, ipaddrtoa(&so->peer_addr, NULL));
		fmt->dict_item(f, "peerport");
		fmt->value_uint(f, so->peer_port);
	}

	fmt->dict_item(f, "subject");
	logevt_process(fmt, f,
	               &so->subject, 0,
	               so->subject_image_exec);

	logevt_footer(fmt, f);
	return 0;
}

int
logevt_socket_connect(logfmt_t *fmt, FILE *f, void *arg0) {
	socket_connect_t *so = (socket_connect_t *)arg0;

	logevt_header(fmt, f, (logevt_header_t *)arg0);

	if (so->protocol) {
		fmt->dict_item(f, "proto");
		fmt->value_string(f, protocoltoa(so->protocol));
	}

	if (!ipaddr_is_empty(&so->sock_addr)) {
		fmt->dict_item(f, "sockaddr");
		fmt->value_string(f, ipaddrtoa(&so->sock_addr, NULL));
		fmt->dict_item(f, "sockport");
		fmt->value_uint(f, so->sock_port);
	}

	if (!ipaddr_is_empty(&so->peer_addr)) {
		fmt->dict_item(f, "peeraddr");
		fmt->value_string(f, ipaddrtoa(&so->peer_addr, NULL));
		fmt->dict_item(f, "peerport");
		fmt->value_uint(f, so->peer_port);
	}

	fmt->dict_item(f, "subject");
	logevt_process(fmt, f,
	               &so->subject, 0,
	               so->subject_image_exec);

	logevt_footer(fmt, f);
	return 0;
}

