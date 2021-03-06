/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "server.h"
#include "server-helpers.h"
#include "glusterfs3-xdr.h"
#include "glusterfs3.h"
#include "compat-errno.h"

#include "md5.h"

#define SERVER_PATH_MAX  (16 * 1024)

/* Callback function section */
int
server_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct statvfs *buf)
{
        gfs3_statfs_rsp   rsp = {0,};
        rpcsvc_request_t *req = NULL;

        req           = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        if (op_ret >= 0) {
                gf_statfs_from_statfs (&rsp.statfs, buf);
        }

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_statfs_rsp);

        return 0;
}

int
server_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno,
                   inode_t *inode, struct iatt *stbuf, dict_t *dict,
                   struct iatt *postparent)
{
        rpcsvc_request_t *req        = NULL;
        server_state_t   *state      = NULL;
        inode_t          *root_inode = NULL;
        inode_t          *link_inode = NULL;
        loc_t             fresh_loc  = {0,};
        gfs3_lookup_rsp   rsp        = {0, };
        int32_t           ret        = -1;

        state = CALL_STATE(frame);

        req = frame->local;

        if (state->is_revalidate == 1 && op_ret == -1) {
                state->is_revalidate = 2;
                loc_copy (&fresh_loc, &state->loc);
                inode_unref (fresh_loc.inode);
                fresh_loc.inode = inode_new (state->itable);

                STACK_WIND (frame, server_lookup_cbk, BOUND_XL (frame),
                            BOUND_XL (frame)->fops->lookup,
                            &fresh_loc, state->dict);

                loc_wipe (&fresh_loc);
                return 0;
        }

        if (dict) {
                rsp.dict.dict_len = dict_serialized_length (dict);
                if (rsp.dict.dict_len < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "%s (%"PRId64"): failed to get serialized "
                                "length of reply dict",
                                state->loc.path, state->loc.inode->ino);
                        op_ret   = -1;
                        op_errno = EINVAL;
                        rsp.dict.dict_len = 0;
                }
        }

        if ((op_ret >= 0) && dict) {
                rsp.dict.dict_val = GF_CALLOC (1, rsp.dict.dict_len,
                                               gf_server_mt_rsp_buf_t);
                if (!rsp.dict.dict_val) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                        rsp.dict.dict_len = 0;
                        goto out;
                }
                ret = dict_serialize (dict, rsp.dict.dict_val);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "%s (%"PRId64"): failed to serialize reply dict",
                                state->loc.path, state->loc.inode->ino);
                        op_ret = -1;
                        op_errno = -ret;
                        rsp.dict.dict_len = 0;
                }
        }

        gf_stat_from_iatt (&rsp.postparent, postparent);

        if (op_ret == 0) {
                root_inode = BOUND_XL(frame)->itable->root;
                if (inode == root_inode) {
                        /* we just looked up root ("/") */
                        stbuf->ia_ino = 1;
                        if (inode->ia_type == 0)
                                inode->ia_type = stbuf->ia_type;
                }

                gf_stat_from_iatt (&rsp.stat, stbuf);

                if (inode->ino != 1) {
                        link_inode = inode_link (inode, state->loc.parent,
                                                 state->loc.name, stbuf);
                        inode_lookup (link_inode);
                        inode_unref (link_inode);
                }
        } else {
                if (state->is_revalidate && op_errno == ENOENT) {
                        if (state->loc.inode->ino != 1) {
                                inode_unlink (state->loc.inode,
                                              state->loc.parent,
                                              state->loc.name);
                        }
                }

                gf_log (this->name,
                        (op_errno == ENOENT ? GF_LOG_TRACE : GF_LOG_DEBUG),
                        "%"PRId64": LOOKUP %s (%"PRId64") ==> %"PRId32" (%s)",
                        frame->root->unique, state->loc.path,
                        state->loc.inode ? state->loc.inode->ino : 0,
                        op_ret, strerror (op_errno));
        }
out:
        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret   = op_ret;
        rsp.op_errno = gf_errno_to_error (op_errno);

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (gfs_serialize_t)xdr_serialize_lookup_rsp);

        if (rsp.dict.dict_val)
                GF_FREE (rsp.dict.dict_val);

        return 0;
}


int
server_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct flock *lock)
{
        gfs3_lk_rsp       rsp   = {0,};
        rpcsvc_request_t *req   = NULL;
        server_state_t   *state = NULL;

        req           = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        state = CALL_STATE(frame);

        if (op_ret == 0) {
                switch (lock->l_type) {
                case F_RDLCK:
                        lock->l_type = GF_LK_F_RDLCK;
                        break;
                case F_WRLCK:
                        lock->l_type = GF_LK_F_WRLCK;
                        break;
                case F_UNLCK:
                        lock->l_type = GF_LK_F_UNLCK;
                        break;
                default:
                        gf_log (this->name, GF_LOG_ERROR,
                                "Unknown lock type: %"PRId32"!", lock->l_type);
                        break;
                }

                gf_flock_from_flock (&rsp.flock, lock);
        } else if (op_errno != ENOSYS) {
                gf_log (this->name, GF_LOG_TRACE,
                        "%"PRId64": LK %"PRId64" (%"PRId64") ==> %"PRId32" (%s)",
                        frame->root->unique, state->resolve.fd_no,
                        state->fd ? state->fd->inode->ino : 0, op_ret,
                        strerror (op_errno));
        }

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_lk_rsp);

        return 0;
}


int
server_inodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno)
{
        gf_common_rsp        rsp   = {0,};
        server_connection_t *conn  = NULL;
        server_state_t      *state = NULL;
        rpcsvc_request_t    *req   = NULL;

        req           = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        conn  = SERVER_CONNECTION(frame);
        state = CALL_STATE(frame);

        if (op_ret >= 0) {
                if (state->flock.l_type == F_UNLCK)
                        gf_del_locker (conn->ltable, state->volume,
                                       &state->loc, NULL, frame->root->pid);
                else
                        gf_add_locker (conn->ltable, state->volume,
                                       &state->loc, NULL, frame->root->pid);
        } else if (op_errno != ENOSYS) {
                gf_log (this->name, GF_LOG_TRACE,
                        "%"PRId64": INODELK %s (%"PRId64") ==> %"PRId32" (%s)",
                        frame->root->unique, state->loc.path,
                        state->loc.inode ? state->loc.inode->ino : 0, op_ret,
                        strerror (op_errno));
        }

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_common_rsp);

        return 0;
}


int
server_finodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno)
{
        gf_common_rsp        rsp   = {0,};
        server_state_t      *state = NULL;
        server_connection_t *conn  = NULL;
        rpcsvc_request_t    *req   = NULL;

        req           = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        conn = SERVER_CONNECTION(frame);
        state = CALL_STATE(frame);

        if (op_ret >= 0) {
                if (state->flock.l_type == F_UNLCK)
                        gf_del_locker (conn->ltable, state->volume,
                                       NULL, state->fd,
                                       frame->root->pid);
                else
                        gf_add_locker (conn->ltable, state->volume,
                                       NULL, state->fd,
                                       frame->root->pid);
        } else if (op_errno != ENOSYS) {
                gf_log (this->name, GF_LOG_TRACE,
                        "%"PRId64": FINODELK %"PRId64" (%"PRId64") ==> %"PRId32" (%s)",
                        frame->root->unique, state->resolve.fd_no,
                        state->fd ? state->fd->inode->ino : 0, op_ret,
                        strerror (op_errno));
        }

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_common_rsp);

        return 0;
}

int
server_entrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno)
{
        server_connection_t *conn  = NULL;
        server_state_t      *state = NULL;
        rpcsvc_request_t    *req   = NULL;
        gf_common_rsp        rsp   = {0,};

        req           = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        conn  = SERVER_CONNECTION(frame);
        state = CALL_STATE(frame);

        if (op_ret >= 0) {
                if (state->cmd == ENTRYLK_UNLOCK)
                        gf_del_locker (conn->ltable, state->volume,
                                       &state->loc, NULL, frame->root->pid);
                else
                        gf_add_locker (conn->ltable, state->volume,
                                       &state->loc, NULL, frame->root->pid);
        } else if (op_errno != ENOSYS) {
                gf_log (this->name, GF_LOG_TRACE,
                        "%"PRId64": INODELK %s (%"PRId64") ==> %"PRId32" (%s)",
                        frame->root->unique, state->loc.path,
                        state->loc.inode ? state->loc.inode->ino : 0, op_ret,
                        strerror (op_errno));
        }

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_common_rsp);
        return 0;
}


int
server_fentrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno)
{
        gf_common_rsp        rsp   = {0,};
        server_connection_t *conn  = NULL;
        server_state_t      *state = NULL;
        rpcsvc_request_t    *req   = NULL;

        req           = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        conn  = SERVER_CONNECTION(frame);
        state = CALL_STATE(frame);
        if (op_ret >= 0) {
                if (state->cmd == ENTRYLK_UNLOCK)
                        gf_del_locker (conn->ltable, state->volume,
                                       NULL, state->fd, frame->root->pid);
                else
                        gf_add_locker (conn->ltable, state->volume,
                                       NULL, state->fd, frame->root->pid);
        } else if (op_errno != ENOSYS) {
                gf_log (this->name, GF_LOG_TRACE,
                        "%"PRId64": FENTRYLK %"PRId64" (%"PRId64") "
                        " ==> %"PRId32" (%s)",
                        frame->root->unique, state->resolve.fd_no,
                        state->fd ? state->fd->inode->ino : 0, op_ret,
                        strerror (op_errno));
        }

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_common_rsp);

        return 0;
}


int
server_access_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno)
{
        gf_common_rsp     rsp = {0,};
        rpcsvc_request_t *req = NULL;

        req           = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_common_rsp);

        return 0;
}

int
server_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                  struct iatt *postparent)
{
        gfs3_rmdir_rsp    rsp    = {0,};
        server_state_t   *state  = NULL;
        inode_t          *parent = NULL;
        rpcsvc_request_t *req    = NULL;

        req           = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        state = CALL_STATE(frame);

        if (op_ret == 0) {
                inode_unlink (state->loc.inode, state->loc.parent,
                              state->loc.name);
                parent = inode_parent (state->loc.inode, 0, NULL);
                if (parent)
                        inode_unref (parent);
                else
                        inode_forget (state->loc.inode, 0);

                gf_stat_from_iatt (&rsp.preparent, preparent);
                gf_stat_from_iatt (&rsp.postparent, postparent);
        } else {
                gf_log (this->name, GF_LOG_TRACE,
                        "%"PRId64": RMDIR %s (%"PRId64") ==> %"PRId32" (%s)",
                        frame->root->unique, state->loc.path,
                        state->loc.inode ? state->loc.inode->ino : 0,
                        op_ret, strerror (op_errno));
        }

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_rmdir_rsp);

        return 0;
}

int
server_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, inode_t *inode,
                  struct iatt *stbuf, struct iatt *preparent,
                  struct iatt *postparent)
{
        gfs3_mkdir_rsp    rsp        = {0,};
        server_state_t   *state      = NULL;
        inode_t          *link_inode = NULL;
        rpcsvc_request_t *req        = NULL;

        req           = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        state = CALL_STATE(frame);
        if (op_ret >= 0) {
                gf_stat_from_iatt (&rsp.stat, stbuf);
                gf_stat_from_iatt (&rsp.preparent, preparent);
                gf_stat_from_iatt (&rsp.postparent, postparent);

                link_inode = inode_link (inode, state->loc.parent,
                                         state->loc.name, stbuf);
                inode_lookup (link_inode);
                inode_unref (link_inode);
        } else {
                gf_log (this->name, GF_LOG_TRACE,
                        "%"PRId64": MKDIR %s  ==> %"PRId32" (%s)",
                        frame->root->unique, state->loc.path,
                        op_ret, strerror (op_errno));
        }

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_mkdir_rsp);

        return 0;
}

int
server_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno,
                  inode_t *inode, struct iatt *stbuf, struct iatt *preparent,
                  struct iatt *postparent)
{
        gfs3_mknod_rsp    rsp        = {0,};
        server_state_t   *state      = NULL;
        inode_t          *link_inode = NULL;
        rpcsvc_request_t *req        = NULL;

        req           = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        state = CALL_STATE(frame);
        if (op_ret >= 0) {
                gf_stat_from_iatt (&rsp.stat, stbuf);
                gf_stat_from_iatt (&rsp.preparent, preparent);
                gf_stat_from_iatt (&rsp.postparent, postparent);

                link_inode = inode_link (inode, state->loc.parent,
                                         state->loc.name, stbuf);
                inode_lookup (link_inode);
                inode_unref (link_inode);
        } else {
                gf_log (this->name, GF_LOG_TRACE,
                        "%"PRId64": MKNOD %s ==> %"PRId32" (%s)",
                        frame->root->unique, state->loc.path,
                        op_ret, strerror (op_errno));
        }

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_mknod_rsp);


        return 0;
}

int
server_fsyncdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno)
{
        gf_common_rsp     rsp   = {0,};
        server_state_t   *state = NULL;
        rpcsvc_request_t *req   = NULL;

        req           = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        state = CALL_STATE(frame);

        if (op_ret < 0) {
                gf_log (this->name, GF_LOG_TRACE,
                        "%"PRId64": FSYNCDIR %"PRId64" (%"PRId64") ==> %"PRId32" (%s)",
                        frame->root->unique, state->resolve.fd_no,
                        state->fd ? state->fd->inode->ino : 0, op_ret,
                        strerror (op_errno));
        }

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_common_rsp);

        return 0;
}

int
server_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, gf_dirent_t *entries)
{
        gfs3_readdir_rsp  rsp   = {0,};
        server_state_t   *state = NULL;
        rpcsvc_request_t *req   = NULL;
        int               ret   = 0;

        req           = frame->local;

        state = CALL_STATE(frame);
        if (op_ret > 0) {
                ret = serialize_rsp_dirent (entries, &rsp);
                if (ret == -1) {
                        op_ret   = -1;
                        op_errno = ENOMEM;
                        goto unwind;
                }
        } else {
                gf_log (this->name, GF_LOG_TRACE,
                        "%"PRId64": READDIR %"PRId64" (%"PRId64") ==> %"PRId32" (%s)",
                        frame->root->unique, state->resolve.fd_no,
                        state->fd ? state->fd->inode->ino : 0, op_ret,
                        strerror (op_errno));
        }
unwind:
        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_readdir_rsp);

        readdir_rsp_cleanup (&rsp);

        return 0;
}


int
server_releasedir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno)
{
        gf_common_rsp     rsp = {0,};
        rpcsvc_request_t *req = NULL;

        req           = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_common_rsp);

        return 0;
}

int
server_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, fd_t *fd)
{
        server_connection_t *conn  = NULL;
        server_state_t      *state = NULL;
        rpcsvc_request_t    *req   = NULL;
        gfs3_opendir_rsp     rsp   = {0,};
        uint64_t             fd_no = 0;

        conn = SERVER_CONNECTION (frame);
        state = CALL_STATE (frame);

        if (op_ret >= 0) {
                fd_bind (fd);

                fd_no = gf_fd_unused_get (conn->fdtable, fd);
                fd_ref (fd); // on behalf of the client
        } else {
                gf_log (this->name, GF_LOG_TRACE,
                        "%"PRId64": OPENDIR %s (%"PRId64") ==> %"PRId32" (%s)",
                        frame->root->unique, state->loc.path,
                        state->loc.inode ? state->loc.inode->ino : 0,
                        op_ret, strerror (op_errno));
        }

        req           = frame->local;

        rsp.fd        = fd_no;
        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_opendir_rsp);

        return 0;
}

int
server_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno)
{
        gf_common_rsp     rsp = {0,};
        rpcsvc_request_t *req = NULL;

        req           = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_common_rsp);

        return 0;
}

int
server_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *dict)
{
        gfs3_getxattr_rsp  rsp   = {0,};
        int32_t            len   = 0;
        int32_t            ret   = -1;
        rpcsvc_request_t  *req   = NULL;
        server_state_t    *state = NULL;

        state = CALL_STATE (frame);

        if (op_ret >= 0) {
                len = dict_serialized_length (dict);
                if (len < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "%s (%"PRId64"): failed to get serialized length of "
                                "reply dict",
                                state->loc.path, state->resolve.ino);
                        op_ret   = -1;
                        op_errno = EINVAL;
                        len = 0;
                        goto out;
                }

                rsp.dict.dict_val = GF_CALLOC (len, sizeof (char),
                                               gf_server_mt_rsp_buf_t);
                if (!rsp.dict.dict_val) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                        len = 0;
                        goto out;
                }
                ret = dict_serialize (dict, rsp.dict.dict_val);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "%s (%"PRId64"): failed to serialize reply dict",
                                state->loc.path, state->resolve.ino);
                        op_ret   = -1;
                        op_errno = EINVAL;
                        len = 0;
                }
        }
out:
        req               = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret        = op_ret;
        rsp.op_errno      = gf_errno_to_error (op_errno);
        rsp.dict.dict_len = len;

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_getxattr_rsp);

        if (rsp.dict.dict_val)
                GF_FREE (rsp.dict.dict_val);

        return 0;
}


int
server_fgetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *dict)
{
        gfs3_fgetxattr_rsp  rsp   = {0,};
        int32_t             len   = 0;
        int32_t             ret   = -1;
        server_state_t     *state = NULL;
        rpcsvc_request_t   *req   = NULL;

        state = CALL_STATE (frame);

        if (op_ret >= 0) {
                len = dict_serialized_length (dict);
                if (len < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "%s (%"PRId64"): failed to get serialized "
                                "length of reply dict",
                                state->loc.path, state->resolve.ino);
                        op_ret   = -1;
                        op_errno = EINVAL;
                        len = 0;
                        goto out;
                }
                rsp.dict.dict_val = GF_CALLOC (1, len, gf_server_mt_rsp_buf_t);
                if (!rsp.dict.dict_val) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                        len = 0;
                        goto out;
                }
                ret = dict_serialize (dict, rsp.dict.dict_val);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "%s (%"PRId64"): failed to serialize reply dict",
                                state->loc.path, state->resolve.ino);
                        op_ret = -1;
                        op_errno = -ret;
                        len = 0;
                }
        }

out:
        req               = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret        = op_ret;
        rsp.op_errno      = gf_errno_to_error (op_errno);
        rsp.dict.dict_len = len;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_fgetxattr_rsp);

        if (rsp.dict.dict_val)
                GF_FREE (rsp.dict.dict_val);

        return 0;
}

int
server_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno)
{
        gf_common_rsp     rsp = {0,};
        rpcsvc_request_t *req = NULL;

        req           = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_common_rsp);

        return 0;
}


int
server_fsetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno)
{
        gf_common_rsp     rsp = {0,};
        rpcsvc_request_t *req = NULL;

        req           = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_common_rsp);

        return 0;
}

int
server_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *stbuf,
                   struct iatt *preoldparent, struct iatt *postoldparent,
                   struct iatt *prenewparent, struct iatt *postnewparent)
{
        gfs3_rename_rsp   rsp   = {0,};
        server_state_t   *state = NULL;
        rpcsvc_request_t *req   = NULL;

        req           = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        state = CALL_STATE(frame);

        if (op_ret == 0) {
                stbuf->ia_ino  = state->loc.inode->ino;
                stbuf->ia_type = state->loc.inode->ia_type;

                gf_log (state->conn->bound_xl->name, GF_LOG_TRACE,
                        "%"PRId64": RENAME_CBK (%"PRId64") %"PRId64"/%s "
                        "==> %"PRId64"/%s",
                        frame->root->unique, state->loc.inode->ino,
                        state->loc.parent->ino, state->loc.name,
                        state->loc2.parent->ino, state->loc2.name);

                inode_rename (state->itable,
                              state->loc.parent, state->loc.name,
                              state->loc2.parent, state->loc2.name,
                              state->loc.inode, stbuf);
                gf_stat_from_iatt (&rsp.stat, stbuf);

                gf_stat_from_iatt (&rsp.preoldparent, preoldparent);
                gf_stat_from_iatt (&rsp.postoldparent, postoldparent);

                gf_stat_from_iatt (&rsp.prenewparent, prenewparent);
                gf_stat_from_iatt (&rsp.postnewparent, postnewparent);
        }

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_rename_rsp);

        return 0;
}

int
server_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                   struct iatt *postparent)
{
        gfs3_unlink_rsp   rsp    = {0,};
        server_state_t   *state  = NULL;
        inode_t          *parent = NULL;
        rpcsvc_request_t *req    = NULL;

        req           = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        state = CALL_STATE(frame);

        if (op_ret == 0) {
                gf_log (state->conn->bound_xl->name, GF_LOG_TRACE,
                        "%"PRId64": UNLINK_CBK %"PRId64"/%s (%"PRId64")",
                        frame->root->unique, state->loc.parent->ino,
                        state->loc.name, state->loc.inode->ino);

                inode_unlink (state->loc.inode, state->loc.parent,
                              state->loc.name);

                parent = inode_parent (state->loc.inode, 0, NULL);
                if (parent)
                        inode_unref (parent);
                else
                        inode_forget (state->loc.inode, 0);

                gf_stat_from_iatt (&rsp.preparent, preparent);
                gf_stat_from_iatt (&rsp.postparent, postparent);

        } else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "%"PRId64": UNLINK %s (%"PRId64") ==> %"PRId32" (%s)",
                        frame->root->unique, state->loc.path,
                        state->loc.inode ? state->loc.inode->ino : 0,
                        op_ret, strerror (op_errno));
        }

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_unlink_rsp);

        return 0;
}

int
server_symlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, inode_t *inode,
                    struct iatt *stbuf, struct iatt *preparent,
                    struct iatt *postparent)
{
        gfs3_symlink_rsp  rsp        = {0,};
        server_state_t   *state      = NULL;
        inode_t          *link_inode = NULL;
        rpcsvc_request_t *req        = NULL;

        req           = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        state = CALL_STATE(frame);
        if (op_ret >= 0) {
                gf_stat_from_iatt (&rsp.stat, stbuf);
                gf_stat_from_iatt (&rsp.preparent, preparent);
                gf_stat_from_iatt (&rsp.postparent, postparent);

                link_inode = inode_link (inode, state->loc.parent,
                                         state->loc.name, stbuf);
                inode_lookup (link_inode);
                inode_unref (link_inode);
        } else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "%"PRId64": SYMLINK %s (%"PRId64") ==> %"PRId32" (%s)",
                        frame->root->unique, state->loc.path,
                        state->loc.inode ? state->loc.inode->ino : 0,
                        op_ret, strerror (op_errno));
        }

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_symlink_rsp);

        return 0;
}


int
server_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, inode_t *inode,
                 struct iatt *stbuf, struct iatt *preparent,
                 struct iatt *postparent)
{
        gfs3_link_rsp     rsp        = {0,};
        server_state_t   *state      = NULL;
        inode_t          *link_inode = NULL;
        rpcsvc_request_t *req        = NULL;

        req           = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        state = CALL_STATE(frame);

        if (op_ret == 0) {
                stbuf->ia_ino = state->loc.inode->ino;

                gf_stat_from_iatt (&rsp.stat, stbuf);
                gf_stat_from_iatt (&rsp.preparent, preparent);
                gf_stat_from_iatt (&rsp.postparent, postparent);

                gf_log (state->conn->bound_xl->name, GF_LOG_TRACE,
                        "%"PRId64": LINK (%"PRId64") %"PRId64"/%s ==> %"PRId64"/%s",
                        frame->root->unique, inode->ino,
                        state->loc2.parent->ino,
                        state->loc2.name, state->loc.parent->ino,
                        state->loc.name);

                link_inode = inode_link (inode, state->loc2.parent,
                                         state->loc2.name, stbuf);
                inode_unref (link_inode);
        } else {
                gf_log (state->conn->bound_xl->name, GF_LOG_DEBUG,
                        "%"PRId64": LINK (%"PRId64") %"PRId64"/%s ==> %"PRId64"/%s "
                        " ==> %"PRId32" (%s)",
                        frame->root->unique, state->resolve2.ino,
                        state->resolve2.par,
                        state->resolve2.bname, state->resolve.par,
                        state->resolve.bname,
                        op_ret, strerror (op_errno));
        }

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_link_rsp);

        return 0;
}

int
server_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                     struct iatt *postbuf)
{
        gfs3_truncate_rsp  rsp   = {0,};
        server_state_t    *state = NULL;
        rpcsvc_request_t  *req   = NULL;

        req           = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        state = CALL_STATE (frame);

        if (op_ret == 0) {
                gf_stat_from_iatt (&rsp.prestat, prebuf);
                gf_stat_from_iatt (&rsp.poststat, postbuf);
        } else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "%"PRId64": TRUNCATE %s (%"PRId64") ==> %"PRId32" (%s)",
                        frame->root->unique, state->loc.path,
                        state->loc.inode ? state->loc.inode->ino : 0,
                        op_ret, strerror (op_errno));
        }

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_truncate_rsp);

        return 0;
}

int
server_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *stbuf)
{
        gfs3_fstat_rsp    rsp   = {0,};
        server_state_t   *state = NULL;
        rpcsvc_request_t *req   = NULL;

        req           = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        state = CALL_STATE(frame);

        if (op_ret == 0) {
                gf_stat_from_iatt (&rsp.stat, stbuf);
        } else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "%"PRId64": FSTAT %"PRId64" (%"PRId64") ==> %"PRId32" (%s)",
                        frame->root->unique, state->resolve.fd_no,
                        state->fd ? state->fd->inode->ino : 0, op_ret,
                        strerror (op_errno));
        }

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_fstat_rsp);

        return 0;
}

int
server_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                      struct iatt *postbuf)
{
        gfs3_ftruncate_rsp  rsp   = {0};
        server_state_t     *state = NULL;
        rpcsvc_request_t   *req   = NULL;

        req           = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        state = CALL_STATE (frame);

        if (op_ret == 0) {
                gf_stat_from_iatt (&rsp.prestat, prebuf);
                gf_stat_from_iatt (&rsp.poststat, postbuf);
        } else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "%"PRId64": FTRUNCATE %"PRId64" (%"PRId64") ==> %"PRId32" (%s)",
                        frame->root->unique, state->resolve.fd_no,
                        state->fd ? state->fd->inode->ino : 0, op_ret,
                        strerror (op_errno));
        }

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_ftruncate_rsp);

        return 0;
}

int
server_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno)
{
        gf_common_rsp     rsp   = {0,};
        server_state_t   *state = NULL;
        rpcsvc_request_t *req   = NULL;

        req           = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        state = CALL_STATE(frame);
        if (op_ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "%"PRId64": FLUSH %"PRId64" (%"PRId64") ==> %"PRId32" (%s)",
                        frame->root->unique, state->resolve.fd_no,
                        state->fd ? state->fd->inode->ino : 0, op_ret,
                        strerror (op_errno));
        }

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_common_rsp);


        return 0;
}

int
server_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                  struct iatt *postbuf)
{
        gfs3_fsync_rsp    rsp   = {0,};
        server_state_t   *state = NULL;
        rpcsvc_request_t *req   = NULL;

        req           = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        state = CALL_STATE(frame);

        if (op_ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "%"PRId64": FSYNC %"PRId64" (%"PRId64") ==> %"PRId32" (%s)",
                        frame->root->unique, state->resolve.fd_no,
                        state->fd ? state->fd->inode->ino : 0, op_ret,
                        strerror (op_errno));
        } else {
                gf_stat_from_iatt (&(rsp.prestat), prebuf);
                gf_stat_from_iatt (&(rsp.poststat), postbuf);
        }

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_fsync_rsp);

        return 0;
}

int
server_release_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno)
{
        gf_common_rsp     rsp = {0,};
        rpcsvc_request_t *req = NULL;

        req           = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_common_rsp);
        return 0;
}


int
server_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                   struct iatt *postbuf)
{
        gfs3_write_rsp    rsp   = {0,};
        server_state_t   *state = NULL;
        rpcsvc_request_t *req   = NULL;

        req           = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        state = CALL_STATE(frame);
        if (op_ret >= 0) {
                gf_stat_from_iatt (&rsp.prestat, prebuf);
                gf_stat_from_iatt (&rsp.poststat, postbuf);
        } else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "%"PRId64": WRITEV %"PRId64" (%"PRId64") ==> %"PRId32" (%s)",
                        frame->root->unique, state->resolve.fd_no,
                        state->fd ? state->fd->inode->ino : 0, op_ret,
                        strerror (op_errno));
        }

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_writev_rsp);

        return 0;
}


int
server_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno,
                  struct iovec *vector, int32_t count,
                  struct iatt *stbuf, struct iobref *iobref)
{
        gfs3_read_rsp     rsp   = {0,};
        server_state_t   *state = NULL;
        rpcsvc_request_t *req   = NULL;

        req           = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        state = CALL_STATE(frame);
        if (op_ret >= 0) {
                gf_stat_from_iatt (&rsp.stat, stbuf);
                rsp.size = op_ret;
        } else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "%"PRId64": READV %"PRId64" (%"PRId64") ==> %"PRId32" (%s)",
                        frame->root->unique, state->resolve.fd_no,
                        state->fd ? state->fd->inode->ino : 0, op_ret,
                        strerror (op_errno));
        }

        server_submit_reply (frame, req, &rsp, vector, count, iobref,
                             xdr_serialize_readv_rsp);

        return 0;
}

int
server_rchecksum_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno,
                      uint32_t weak_checksum, uint8_t *strong_checksum)
{
        gfs3_rchecksum_rsp  rsp = {0,};
        rpcsvc_request_t   *req = NULL;

        req           = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        if (op_ret >= 0) {
                rsp.weak_checksum = weak_checksum;

                rsp.strong_checksum.strong_checksum_val = (char *)strong_checksum;
                rsp.strong_checksum.strong_checksum_len = MD5_DIGEST_LEN;
        }

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_rchecksum_rsp);

        return 0;
}


int
server_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, fd_t *fd)
{
        server_connection_t *conn  = NULL;
        server_state_t      *state = NULL;
        rpcsvc_request_t    *req   = NULL;
        uint64_t             fd_no = 0;
        gfs3_open_rsp        rsp   = {0,};

        conn = SERVER_CONNECTION (frame);
        state = CALL_STATE (frame);

        if (op_ret >= 0) {
                fd_bind (fd);
                fd_no = gf_fd_unused_get (conn->fdtable, fd);
                fd_ref (fd);
        } else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "%"PRId64": OPEN %s (%"PRId64") ==> %"PRId32" (%s)",
                        frame->root->unique, state->loc.path,
                        state->loc.inode ? state->loc.inode->ino : 0,
                        op_ret, strerror (op_errno));
        }

        req           = frame->local;

        rsp.fd        = fd_no;
        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_open_rsp);
        return 0;
}


int
server_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno,
                   fd_t *fd, inode_t *inode, struct iatt *stbuf,
                   struct iatt *preparent, struct iatt *postparent)
{
        server_connection_t *conn       = NULL;
        server_state_t      *state      = NULL;
        inode_t             *link_inode = NULL;
        rpcsvc_request_t    *req        = NULL;
        uint64_t             fd_no      = 0;
        gfs3_create_rsp      rsp        = {0,};

        conn = SERVER_CONNECTION (frame);
        state = CALL_STATE (frame);

        if (op_ret >= 0) {
                gf_log (state->conn->bound_xl->name, GF_LOG_TRACE,
                        "%"PRId64": CREATE %"PRId64"/%s (%"PRId64")",
                        frame->root->unique, state->loc.parent->ino,
                        state->loc.name, stbuf->ia_ino);

                link_inode = inode_link (inode, state->loc.parent,
                                         state->loc.name, stbuf);

                if (link_inode != inode) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "create(%s) inode (ptr=%p, ino=%"PRId64", "
                                "gen=%"PRId64") found conflict (ptr=%p, "
                                "ino=%"PRId64", gen=%"PRId64")",
                                state->loc.path, inode, inode->ino,
                                inode->generation, link_inode,
                                link_inode->ino, link_inode->generation);

                        /*
                           VERY racy code (if used anywhere else)
                           -- don't do this without understanding
                        */

                        inode_unref (fd->inode);
                        fd->inode = inode_ref (link_inode);
                }

                inode_lookup (link_inode);
                inode_unref (link_inode);

                fd_bind (fd);

                fd_no = gf_fd_unused_get (conn->fdtable, fd);
                fd_ref (fd);

                if ((fd_no < 0) || (fd == 0)) {
                        op_ret = fd_no;
                        op_errno = errno;
                }

                gf_stat_from_iatt (&rsp.stat, stbuf);
                gf_stat_from_iatt (&rsp.preparent, preparent);
                gf_stat_from_iatt (&rsp.postparent, postparent);
        } else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "%"PRId64": CREATE %s (%"PRId64") ==> %"PRId32" (%s)",
                        frame->root->unique, state->loc.path,
                        state->loc.inode ? state->loc.inode->ino : 0,
                        op_ret, strerror (op_errno));
        }

        req           = frame->local;

        rsp.fd        = fd_no;
        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_create_rsp);

        return 0;
}

int
server_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, const char *buf,
                     struct iatt *stbuf)
{
        gfs3_readlink_rsp  rsp   = {0,};
        server_state_t    *state = NULL;
        rpcsvc_request_t  *req   = NULL;

        req           = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);


        state  = CALL_STATE(frame);

        if (op_ret >= 0) {
                gf_stat_from_iatt (&rsp.buf, stbuf);
                rsp.path = (char *)buf;
        } else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "%"PRId64": READLINK %s (%"PRId64") ==> %"PRId32" (%s)",
                        frame->root->unique, state->loc.path,
                        state->loc.inode ? state->loc.inode->ino : 0,
                        op_ret, strerror (op_errno));
        }

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_readlink_rsp);

        return 0;
}

int
server_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *stbuf)
{
        gfs3_stat_rsp     rsp   = {0,};
        server_state_t   *state = NULL;
        rpcsvc_request_t *req   = NULL;

        req           = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        state  = CALL_STATE (frame);

        if (op_ret == 0) {
                gf_stat_from_iatt (&rsp.stat, stbuf);
        } else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "%"PRId64": STAT %s (%"PRId64") ==> %"PRId32" (%s)",
                        frame->root->unique, state->loc.path,
                        state->loc.inode ? state->loc.inode->ino : 0,
                        op_ret, strerror (op_errno));
        }

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_stat_rsp);

        return 0;
}


int
server_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno,
                    struct iatt *statpre, struct iatt *statpost)
{
        gfs3_setattr_rsp  rsp   = {0,};
        server_state_t   *state = NULL;
        rpcsvc_request_t *req   = NULL;

        req           = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        state  = CALL_STATE (frame);

        if (op_ret == 0) {
                gf_stat_from_iatt (&rsp.statpre, statpre);
                gf_stat_from_iatt (&rsp.statpost, statpost);
        } else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "%"PRId64": SETATTR %s (%"PRId64") ==> %"PRId32" (%s)",
                        frame->root->unique, state->loc.path,
                        state->loc.inode ? state->loc.inode->ino : 0,
                        op_ret, strerror (op_errno));
        }

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_setattr_rsp);

        return 0;
}

int
server_fsetattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno,
                     struct iatt *statpre, struct iatt *statpost)
{
        gfs3_fsetattr_rsp  rsp   = {0,};
        server_state_t    *state = NULL;
        rpcsvc_request_t  *req   = NULL;

        state  = CALL_STATE (frame);

        if (op_ret == 0) {
                gf_stat_from_iatt (&rsp.statpre, statpre);
                gf_stat_from_iatt (&rsp.statpost, statpost);
        } else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "%"PRId64": FSETATTR %"PRId64" (%"PRId64") ==> "
                        "%"PRId32" (%s)",
                        frame->root->unique, state->resolve.fd_no,
                        state->fd ? state->fd->inode->ino : 0,
                        op_ret, strerror (op_errno));
        }

        req           = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_fsetattr_rsp);

        return 0;
}


int
server_xattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *dict)
{
        gfs3_xattrop_rsp  rsp   = {0,};
        int32_t           len   = 0;
        int32_t           ret   = -1;
        server_state_t   *state = NULL;
        rpcsvc_request_t *req   = NULL;

        state = CALL_STATE (frame);

        if (op_ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "%"PRId64": XATTROP %s (%"PRId64") ==> %"PRId32" (%s)",
                        frame->root->unique, state->loc.path,
                        state->loc.inode ? state->loc.inode->ino : 0,
                        op_ret, strerror (op_errno));
                goto out;
        }

        if ((op_ret >= 0) && dict) {
                len = dict_serialized_length (dict);
                if (len < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "%s (%"PRId64"): failed to get serialized length"
                                " for reply dict",
                                state->loc.path, state->loc.inode->ino);
                        op_ret = -1;
                        op_errno = EINVAL;
                        len = 0;
                        goto out;
                }
                rsp.dict.dict_val = GF_CALLOC (1, len, gf_server_mt_rsp_buf_t);
                if (!rsp.dict.dict_val) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                        len = 0;
                        goto out;
                }
                ret = dict_serialize (dict, rsp.dict.dict_val);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "%s (%"PRId64"): failed to serialize reply dict",
                                state->loc.path, state->loc.inode->ino);
                        op_ret = -1;
                        op_errno = -ret;
                        len = 0;
                }
        }
out:
        req               = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret        = op_ret;
        rsp.op_errno      = gf_errno_to_error (op_errno);
        rsp.dict.dict_len = len;

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_xattrop_rsp);

        if (rsp.dict.dict_val)
                GF_FREE (rsp.dict.dict_val);

        return 0;
}


int
server_fxattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *dict)
{
        gfs3_xattrop_rsp  rsp   = {0,};
        int32_t           len   = 0;
        int32_t           ret   = -1;
        server_state_t   *state = NULL;
        rpcsvc_request_t *req   = NULL;

        state = CALL_STATE(frame);

        if (op_ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "%"PRId64": FXATTROP %"PRId64" (%"PRId64") ==> %"PRId32" (%s)",
                        frame->root->unique, state->resolve.fd_no,
                        state->fd ? state->fd->inode->ino : 0, op_ret,
                        strerror (op_errno));
                goto out;
        }

        if ((op_ret >= 0) && dict) {
                len = dict_serialized_length (dict);
                if (len < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "fd - %"PRId64" (%"PRId64"): failed to get "
                                "serialized length for reply dict",
                                state->resolve.fd_no, state->fd->inode->ino);
                        op_ret = -1;
                        op_errno = EINVAL;
                        len = 0;
                        goto out;
                }
                rsp.dict.dict_val = GF_CALLOC (1, len, gf_server_mt_rsp_buf_t);
                if (!rsp.dict.dict_val) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                        len = 0;
                        goto out;
                }
                ret = dict_serialize (dict, rsp.dict.dict_val);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "fd - %"PRId64" (%"PRId64"): failed to "
                                "serialize reply dict",
                                state->resolve.fd_no, state->fd->inode->ino);
                        op_ret = -1;
                        op_errno = -ret;
                        len = 0;
                }
        }
out:
        req               = frame->local;

        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret        = op_ret;
        rsp.op_errno      = gf_errno_to_error (op_errno);
        rsp.dict.dict_len = len;

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_fxattrop_rsp);

        if (rsp.dict.dict_val)
                GF_FREE (rsp.dict.dict_val);

        return 0;
}


int
server_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, gf_dirent_t *entries)
{
        gfs3_readdirp_rsp  rsp   = {0,};
        server_state_t    *state = NULL;
        rpcsvc_request_t  *req   = NULL;
        int                ret   = 0;

        req           = frame->local;

        state = CALL_STATE(frame);
        if (op_ret > 0) {
                ret = serialize_rsp_direntp (entries, &rsp);
                if (ret == -1) {
                        op_ret   = -1;
                        op_errno = ENOMEM;
                        goto out;
                }
        } else {
                gf_log (this->name, GF_LOG_TRACE,
                        "%"PRId64": READDIRP %"PRId64" (%"PRId64") ==>"
                        "%"PRId32" (%s)",
                        frame->root->unique, state->resolve.fd_no,
                        state->fd ? state->fd->inode->ino : 0, op_ret,
                        strerror (op_errno));
        }

out:
        rsp.gfs_id    = req->gfs_id;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_readdirp_rsp);

        readdirp_rsp_cleanup (&rsp);

        return 0;
}

/* Resume function section */

int
server_rchecksum_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state    = NULL;
        int             op_ret   = 0;
        int             op_errno = EINVAL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0) {
                op_ret   = state->resolve.op_ret;
                op_errno = state->resolve.op_errno;
                goto err;
        }

        STACK_WIND (frame, server_rchecksum_cbk, bound_xl,
                    bound_xl->fops->rchecksum, state->fd,
                    state->offset, state->size);

        return 0;
err:
        server_rchecksum_cbk (frame, NULL, frame->this, op_ret, op_errno, 0, NULL);

        return 0;

}

int
server_lk_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server_lk_cbk, bound_xl, bound_xl->fops->lk,
                    state->fd, state->cmd, &state->flock);

        return 0;

err:
        server_lk_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                       state->resolve.op_errno, NULL);
        return 0;
}

int
server_rename_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;
        int             op_ret = 0;
        int             op_errno = 0;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0) {
                op_ret   = state->resolve.op_ret;
                op_errno = state->resolve.op_errno;
                goto err;
        }

        if (state->resolve2.op_ret != 0) {
                op_ret   = state->resolve2.op_ret;
                op_errno = state->resolve2.op_errno;
                goto err;
        }

        STACK_WIND (frame, server_rename_cbk,
                    bound_xl, bound_xl->fops->rename,
                    &state->loc, &state->loc2);
        return 0;
err:
        server_rename_cbk (frame, NULL, frame->this, op_ret, op_errno,
                           NULL, NULL, NULL, NULL, NULL);
        return 0;
}


int
server_link_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;
        int             op_ret = 0;
        int             op_errno = 0;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0) {
                op_ret   = state->resolve.op_ret;
                op_errno = state->resolve.op_errno;
                goto err;
        }

        if (state->resolve2.op_ret != 0) {
                op_ret   = state->resolve2.op_ret;
                op_errno = state->resolve2.op_errno;
                goto err;
        }

        state->loc2.inode = inode_ref (state->loc.inode);

        STACK_WIND (frame, server_link_cbk, bound_xl, bound_xl->fops->link,
                    &state->loc, &state->loc2);

        return 0;
err:
        server_link_cbk (frame, NULL, frame->this, op_ret, op_errno,
                         NULL, NULL, NULL, NULL);
        return 0;
}

int
server_symlink_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        state->loc.inode = inode_new (state->itable);

        STACK_WIND (frame, server_symlink_cbk,
                    bound_xl, bound_xl->fops->symlink,
                    state->name, &state->loc);

        return 0;
err:
        server_symlink_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                            state->resolve.op_errno, NULL, NULL, NULL, NULL);
        return 0;
}


int
server_access_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server_access_cbk,
                    bound_xl, bound_xl->fops->access,
                    &state->loc, state->mask);
        return 0;
err:
        server_access_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                           state->resolve.op_errno);
        return 0;
}

int
server_fentrylk_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server_fentrylk_cbk, bound_xl,
                    bound_xl->fops->fentrylk,
                    state->volume, state->fd, state->name,
                    state->cmd, state->type);

        return 0;
err:
        server_fentrylk_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                             state->resolve.op_errno);
        return 0;
}


int
server_entrylk_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server_entrylk_cbk,
                    bound_xl, bound_xl->fops->entrylk,
                    state->volume, &state->loc, state->name,
                    state->cmd, state->type);
        return 0;
err:
        server_entrylk_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                            state->resolve.op_errno);
        return 0;
}


int
server_finodelk_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server_finodelk_cbk, bound_xl,
                    bound_xl->fops->finodelk,
                    state->volume, state->fd, state->cmd, &state->flock);

        return 0;
err:
        server_finodelk_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                             state->resolve.op_errno);

        return 0;
}

int
server_inodelk_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server_inodelk_cbk,
                    bound_xl, bound_xl->fops->inodelk,
                    state->volume, &state->loc, state->cmd, &state->flock);
        return 0;
err:
        server_inodelk_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                            state->resolve.op_errno);
        return 0;
}

int
server_rmdir_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server_rmdir_cbk,
                    bound_xl, bound_xl->fops->rmdir, &state->loc);
        return 0;
err:
        server_rmdir_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                          state->resolve.op_errno, NULL, NULL);
        return 0;
}

int
server_mkdir_resume (call_frame_t *frame, xlator_t *bound_xl)

{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        state->loc.inode = inode_new (state->itable);

        STACK_WIND (frame, server_mkdir_cbk,
                    bound_xl, bound_xl->fops->mkdir,
                    &(state->loc), state->mode);

        return 0;
err:
        server_mkdir_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                          state->resolve.op_errno, NULL, NULL, NULL, NULL);
        return 0;
}


int
server_mknod_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        state->loc.inode = inode_new (state->itable);

        STACK_WIND (frame, server_mknod_cbk,
                    bound_xl, bound_xl->fops->mknod,
                    &(state->loc), state->mode, state->dev);

        return 0;
err:
        server_mknod_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                          state->resolve.op_errno, NULL, NULL, NULL, NULL);
        return 0;
}


int
server_fsyncdir_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server_fsyncdir_cbk,
                    bound_xl,
                    bound_xl->fops->fsyncdir,
                    state->fd, state->flags);
        return 0;

err:
        server_fsyncdir_cbk (frame, NULL, frame->this,
                             state->resolve.op_ret,
                             state->resolve.op_errno);
        return 0;
}


int
 server_readdir_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t  *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server_readdir_cbk,
                    bound_xl,
                    bound_xl->fops->readdir,
                    state->fd, state->size, state->offset);

        return 0;
err:
        server_readdir_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                            state->resolve.op_errno, NULL);
        return 0;
}

int
server_readdirp_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t  *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server_readdirp_cbk, bound_xl,
                    bound_xl->fops->readdirp, state->fd, state->size,
                    state->offset);

        return 0;
err:
        server_readdirp_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                             state->resolve.op_errno, NULL);
        return 0;
}


int
server_opendir_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t  *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        state->fd = fd_create (state->loc.inode, frame->root->pid);

        STACK_WIND (frame, server_opendir_cbk,
                    bound_xl, bound_xl->fops->opendir,
                    &state->loc, state->fd);
        return 0;
err:
        server_opendir_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                            state->resolve.op_errno, NULL);
        return 0;
}


int
server_statfs_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t      *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret !=0)
                goto err;

        STACK_WIND (frame, server_statfs_cbk,
                    bound_xl, bound_xl->fops->statfs,
                    &state->loc);
        return 0;

err:
        server_statfs_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                           state->resolve.op_errno, NULL);
        return 0;
}


int
server_removexattr_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server_removexattr_cbk,
                    bound_xl, bound_xl->fops->removexattr,
                    &state->loc, state->name);
        return 0;
err:
        server_removexattr_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                                state->resolve.op_errno);
        return 0;
}

int
server_fgetxattr_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server_fgetxattr_cbk,
                    bound_xl, bound_xl->fops->fgetxattr,
                    state->fd, state->name);
        return 0;
err:
        server_fgetxattr_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                              state->resolve.op_errno, NULL);
        return 0;
}


int
server_xattrop_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server_xattrop_cbk,
                    bound_xl, bound_xl->fops->xattrop,
                    &state->loc, state->flags, state->dict);
        return 0;
err:
        server_xattrop_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                            state->resolve.op_errno, NULL);
        return 0;
}

int
server_fxattrop_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server_fxattrop_cbk,
                    bound_xl, bound_xl->fops->fxattrop,
                    state->fd, state->flags, state->dict);
        return 0;
err:
        server_fxattrop_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                             state->resolve.op_errno, NULL);
        return 0;
}

int
server_fsetxattr_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server_setxattr_cbk,
                    bound_xl, bound_xl->fops->fsetxattr,
                    state->fd, state->dict, state->flags);
        return 0;
err:
        server_fsetxattr_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                              state->resolve.op_errno);

        return 0;
}

int
server_unlink_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server_unlink_cbk,
                    bound_xl, bound_xl->fops->unlink,
                    &state->loc);
        return 0;
err:
        server_unlink_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                           state->resolve.op_errno, NULL, NULL);
        return 0;
}

int
server_truncate_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server_truncate_cbk,
                    bound_xl, bound_xl->fops->truncate,
                    &state->loc, state->offset);
        return 0;
err:
        server_truncate_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                             state->resolve.op_errno, NULL, NULL);
        return 0;
}



int
server_fstat_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t     *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server_fstat_cbk,
                    bound_xl, bound_xl->fops->fstat,
                    state->fd);
        return 0;
err:
        server_fstat_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                          state->resolve.op_errno, NULL);
        return 0;
}


int
server_setxattr_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server_setxattr_cbk,
                    bound_xl, bound_xl->fops->setxattr,
                    &state->loc, state->dict, state->flags);
        return 0;
err:
        server_setxattr_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                             state->resolve.op_errno);

        return 0;
}


int
server_getxattr_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server_getxattr_cbk,
                    bound_xl, bound_xl->fops->getxattr,
                    &state->loc, state->name);
        return 0;
err:
        server_getxattr_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                             state->resolve.op_errno, NULL);
        return 0;
}


int
server_ftruncate_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t    *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server_ftruncate_cbk,
                    bound_xl, bound_xl->fops->ftruncate,
                    state->fd, state->offset);
        return 0;
err:
        server_ftruncate_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                              state->resolve.op_errno, NULL, NULL);

        return 0;
}


int
server_flush_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t    *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server_flush_cbk,
                    bound_xl, bound_xl->fops->flush, state->fd);
        return 0;
err:
        server_flush_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                          state->resolve.op_errno);

        return 0;
}


int
server_fsync_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t    *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server_fsync_cbk,
                    bound_xl, bound_xl->fops->fsync,
                    state->fd, state->flags);
        return 0;
err:
        server_fsync_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                          state->resolve.op_errno, NULL, NULL);

        return 0;
}

int
server_writev_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t   *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server_writev_cbk,
                    bound_xl, bound_xl->fops->writev,
                    state->fd, state->payload_vector, state->payload_count,
                    state->offset, state->iobref);

        return 0;
err:
        server_writev_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                           state->resolve.op_errno, NULL, NULL);
        return 0;
}


int
server_readv_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t    *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server_readv_cbk,
                    bound_xl, bound_xl->fops->readv,
                    state->fd, state->size, state->offset);

        return 0;
err:
        server_readv_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                          state->resolve.op_errno, NULL, 0, NULL, NULL);
        return 0;
}


int
server_create_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        state->loc.inode = inode_new (state->itable);

        state->fd = fd_create (state->loc.inode, frame->root->pid);
        state->fd->flags = state->flags;

        STACK_WIND (frame, server_create_cbk,
                    bound_xl, bound_xl->fops->create,
                    &(state->loc), state->flags, state->mode, state->fd);

        return 0;
err:
        server_create_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                           state->resolve.op_errno, NULL, NULL, NULL,
                           NULL, NULL);
        return 0;
}


int
server_open_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t  *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        state->fd = fd_create (state->loc.inode, frame->root->pid);
        state->fd->flags = state->flags;

        STACK_WIND (frame, server_open_cbk,
                    bound_xl, bound_xl->fops->open,
                    &state->loc, state->flags, state->fd, 0);

        return 0;
err:
        server_open_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                         state->resolve.op_errno, NULL);
        return 0;
}


int
server_readlink_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server_readlink_cbk,
                    bound_xl, bound_xl->fops->readlink,
                    &state->loc, state->size);
        return 0;
err:
        server_readlink_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                             state->resolve.op_errno, NULL, NULL);
        return 0;
}


int
server_fsetattr_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server_fsetattr_cbk,
                    bound_xl, bound_xl->fops->fsetattr,
                    state->fd, &state->stbuf, state->valid);
        return 0;
err:
        server_fsetattr_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                             state->resolve.op_errno, NULL, NULL);

        return 0;
}


int
server_setattr_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server_setattr_cbk,
                    bound_xl, bound_xl->fops->setattr,
                    &state->loc, &state->stbuf, state->valid);
        return 0;
err:
        server_setattr_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                            state->resolve.op_errno, NULL, NULL);

        return 0;
}


int
server_stat_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server_stat_cbk,
                    bound_xl, bound_xl->fops->stat, &state->loc);
        return 0;
err:
        server_stat_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                         state->resolve.op_errno, NULL);
        return 0;
}

int
server_lookup_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t    *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        if (!state->loc.inode)
                state->loc.inode = inode_new (state->itable);
        else
                state->is_revalidate = 1;

        STACK_WIND (frame, server_lookup_cbk,
                    bound_xl, bound_xl->fops->lookup,
                    &state->loc, state->dict);

        return 0;
err:
        server_lookup_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                           state->resolve.op_errno, NULL, NULL, NULL, NULL);

        return 0;
}




/* Fop section */

int
server_stat (rpcsvc_request_t *req)
{
        server_state_t *state                 = NULL;
        call_frame_t   *frame                 = NULL;
        gfs3_stat_req   args                  = {0,};
        char            path[SERVER_PATH_MAX] = {0,};

        if (!req)
                return 0;

        /* Initialize args first, then decode */
        args.path = path;

        if (!xdr_to_stat_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_STAT;

        state = CALL_STATE (frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type  = RESOLVE_MUST;
        state->resolve.ino   = args.ino;
        state->resolve.gen   = args.gen;
        state->resolve.path  = gf_strdup (args.path);

        resolve_and_resume (frame, server_stat_resume);
out:
        return 0;
}


int
server_setattr (rpcsvc_request_t *req)
{
        server_state_t   *state                 = NULL;
        call_frame_t     *frame                 = NULL;
        gfs3_setattr_req  args                  = {0,};
        char              path[SERVER_PATH_MAX] = {0,};

        if (!req)
                return 0;

        args.path = path;

        if (!xdr_to_setattr_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_SETATTR;

        state = CALL_STATE (frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type  = RESOLVE_MUST;
        state->resolve.ino   = args.ino;
        state->resolve.gen   = args.gen;
        state->resolve.path  = gf_strdup (args.path);

        gf_stat_to_iatt (&args.stbuf, &state->stbuf);
        state->valid = args.valid;

        resolve_and_resume (frame, server_setattr_resume);
out:
        return 0;
}


int
server_fsetattr (rpcsvc_request_t *req)
{
        server_state_t    *state = NULL;
        call_frame_t      *frame = NULL;
        gfs3_fsetattr_req  args  = {0,};

        if (!req)
                return 0;

        if (!xdr_to_fsetattr_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_FSETATTR;

        state = CALL_STATE (frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type   = RESOLVE_MUST;
        state->resolve.fd_no  = args.fd;

        gf_stat_to_iatt (&args.stbuf, &state->stbuf);
        state->valid = args.valid;

        resolve_and_resume (frame, server_fsetattr_resume);
out:
        return 0;
}


int
server_readlink (rpcsvc_request_t *req)
{
        server_state_t    *state                 = NULL;
        call_frame_t      *frame                 = NULL;
        gfs3_readlink_req  args                  = {0,};
        char               path[SERVER_PATH_MAX] = {0,};

        if (!req)
                return 0;

        args.path = path;

        if (!xdr_to_readlink_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_READLINK;

        state = CALL_STATE (frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type = RESOLVE_MUST;
        state->resolve.ino  = args.ino;
        state->resolve.gen  = args.gen;
        state->resolve.path = gf_strdup (args.path);

        state->size  = args.size;

        resolve_and_resume (frame, server_readlink_resume);
out:
        return 0;
}


int
server_create (rpcsvc_request_t *req)
{
        server_state_t      *state                  = NULL;
        call_frame_t        *frame                  = NULL;
        gfs3_create_req      args                   = {0,};
        char                 path[SERVER_PATH_MAX]  = {0,};
        char                 bname[SERVER_PATH_MAX] = {0,};

        if (!req)
                return 0;

        args.path  = path;
        args.bname = bname;

        if (!xdr_to_create_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_CREATE;

        state = CALL_STATE (frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type   = RESOLVE_NOT;
        state->resolve.par    = args.par;
        state->resolve.gen    = args.gen;
        state->resolve.path   = gf_strdup (args.path);
        state->resolve.bname  = gf_strdup (args.bname);
        state->mode           = args.mode;
        state->flags          = gf_flags_to_flags (args.flags);

        resolve_and_resume (frame, server_create_resume);
out:
        return 0;
}


int
server_open (rpcsvc_request_t *req)
{
        server_state_t *state                 = NULL;
        call_frame_t   *frame                 = NULL;
        gfs3_open_req   args                  = {0,};
        char            path[SERVER_PATH_MAX] = {0,};

        if (!req)
                return 0;

        args.path = path;

        if (!xdr_to_open_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_OPEN;

        state = CALL_STATE (frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type  = RESOLVE_MUST;
        state->resolve.ino   = args.ino;
        state->resolve.gen   = args.gen;
        state->resolve.path  = gf_strdup (args.path);

        state->flags = gf_flags_to_flags (args.flags);

        resolve_and_resume (frame, server_open_resume);
out:
        return 0;
}


int
server_readv (rpcsvc_request_t *req)
{
        server_state_t *state = NULL;
        call_frame_t   *frame = NULL;
        gfs3_read_req   args  = {0,};

        if (!req)
                goto out;

        if (!xdr_to_readv_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_READ;

        state = CALL_STATE (frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type   = RESOLVE_MUST;
        state->resolve.fd_no  = args.fd;
        state->size           = args.size;
        state->offset         = args.offset;

        resolve_and_resume (frame, server_readv_resume);
out:
        return 0;
}


int
server_writev (rpcsvc_request_t *req)
{
        server_state_t      *state  = NULL;
        call_frame_t        *frame  = NULL;
        gfs3_write_req       args   = {0,};
        ssize_t              len    = 0;
        int                  i      = 0;

        if (!req)
                return 0;

        len = xdr_to_writev_req (req->msg[0], &args);
        if (len == 0) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_WRITE;

        state = CALL_STATE (frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type  = RESOLVE_MUST;
        state->resolve.fd_no = args.fd;
        state->offset        = args.offset;
        state->iobref        = iobref_ref (req->iobref);

        if (len < req->msg[0].iov_len) {
                state->payload_vector[0].iov_base
                        = (req->msg[0].iov_base + len);
                state->payload_vector[0].iov_len
                        = req->msg[0].iov_len - len;
                state->payload_count = 1;
        }

        for (i = 1; i < req->count; i++) {
                state->payload_vector[state->payload_count++]
                        = req->msg[i];
        }

        for (i = 0; i < state->payload_count; i++) {
                state->size += state->payload_vector[i].iov_len;
        }

        resolve_and_resume (frame, server_writev_resume);
out:
        return 0;
}


int
server_writev_vec (rpcsvc_request_t *req, struct iovec *payload,
                   int payload_count, struct iobref *iobref)
{
        return server_writev (req);
}


int
server_release (rpcsvc_request_t *req)
{
        server_connection_t *conn = NULL;
        gfs3_release_req     args = {0,};
        gf_common_rsp        rsp  = {0,};

        if (!xdr_to_release_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        conn = req->conn->trans->xl_private;
        gf_fd_put (conn->fdtable, args.fd);

        server_submit_reply (NULL, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_common_rsp);
out:
        return 0;
}

int
server_releasedir (rpcsvc_request_t *req)
{
        server_connection_t *conn = NULL;
        gfs3_releasedir_req  args = {0,};
        gf_common_rsp        rsp  = {0,};

        if (!xdr_to_release_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        conn = req->conn->trans->xl_private;
        gf_fd_put (conn->fdtable, args.fd);

        server_submit_reply (NULL, req, &rsp, NULL, 0, NULL,
                             xdr_serialize_common_rsp);
out:
        return 0;
}


int
server_fsync (rpcsvc_request_t *req)
{
        server_state_t *state = NULL;
        call_frame_t   *frame = NULL;
        gfs3_fsync_req  args  = {0,};

        if (!req)
                return 0;

        if (!xdr_to_fsync_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_FSYNC;

        state = CALL_STATE (frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type  = RESOLVE_MUST;
        state->resolve.fd_no = args.fd;
        state->flags         = args.data;

        resolve_and_resume (frame, server_fsync_resume);
out:
        return 0;
}



int
server_flush (rpcsvc_request_t *req)
{
        server_state_t *state = NULL;
        call_frame_t   *frame = NULL;
        gfs3_flush_req  args  = {0,};

        if (!req)
                return 0;

        if (!xdr_to_flush_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_FLUSH;

        state = CALL_STATE (frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type  = RESOLVE_MUST;
        state->resolve.fd_no = args.fd;

        resolve_and_resume (frame, server_flush_resume);
out:
        return 0;
}



int
server_ftruncate (rpcsvc_request_t *req)
{
        server_state_t     *state = NULL;
        call_frame_t       *frame = NULL;
        gfs3_ftruncate_req  args  = {0,};

        if (!req)
                return 0;

        if (!xdr_to_ftruncate_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_FTRUNCATE;

        state = CALL_STATE (frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type   = RESOLVE_MUST;
        state->resolve.fd_no  = args.fd;
        state->offset         = args.offset;

        resolve_and_resume (frame, server_ftruncate_resume);
out:
        return 0;
}


int
server_fstat (rpcsvc_request_t *req)
{
        server_state_t *state = NULL;
        call_frame_t   *frame = NULL;
        gfs3_write_req  args  = {0,};

        if (!req)
                return 0;

        if (!xdr_to_fstat_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_FSTAT;

        state = CALL_STATE (frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type    = RESOLVE_MUST;
        state->resolve.fd_no   = args.fd;

        resolve_and_resume (frame, server_fstat_resume);
out:
        return 0;
}


int
server_truncate (rpcsvc_request_t *req)
{
        server_state_t    *state                 = NULL;
        call_frame_t      *frame                 = NULL;
        gfs3_truncate_req  args                  = {0,};
        char               path[SERVER_PATH_MAX] = {0,};

        if (!req)
                return 0;

        args.path = path;
        if (!xdr_to_truncate_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_TRUNCATE;

        state = CALL_STATE (frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type  = RESOLVE_MUST;
        state->resolve.path  = gf_strdup (args.path);
        state->resolve.ino   = args.ino;
        state->resolve.gen   = args.gen;
        state->offset        = args.offset;

        resolve_and_resume (frame, server_truncate_resume);
out:
        return 0;
}



int
server_unlink (rpcsvc_request_t *req)
{
        server_state_t  *state                  = NULL;
        call_frame_t    *frame                  = NULL;
        gfs3_unlink_req  args                   = {0,};
        char             path[SERVER_PATH_MAX]  = {0,};
        char             bname[SERVER_PATH_MAX] = {0,};

        if (!req)
                return 0;

        args.path  = path;
        args.bname = bname;

        if (!xdr_to_unlink_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_UNLINK;

        state = CALL_STATE (frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type   = RESOLVE_MUST;
        state->resolve.par    = args.par;
        state->resolve.gen    = args.gen;
        state->resolve.path   = gf_strdup (args.path);
        state->resolve.bname  = gf_strdup (args.bname);

        resolve_and_resume (frame, server_unlink_resume);
out:
        return 0;
}


int
server_setxattr (rpcsvc_request_t *req)
{
        server_state_t      *state                 = NULL;
        dict_t              *dict                  = NULL;
        call_frame_t        *frame                 = NULL;
        server_connection_t *conn                  = NULL;
        char                *buf                   = NULL;
        gfs3_setxattr_req    args                  = {0,};
        char                 path[SERVER_PATH_MAX] = {0,};
        char                 dict_val[(16 * 1024)] = {0, };
        int32_t              ret                   = -1;

        if (!req)
                return 0;

        conn = req->conn->trans->xl_private;

        args.path = path;
        args.dict.dict_val = dict_val;

        if (!xdr_to_setxattr_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_SETXATTR;

        state = CALL_STATE (frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type     = RESOLVE_MUST;
        state->resolve.path     = gf_strdup (args.path);
        state->resolve.ino      = args.ino;
        state->resolve.gen      = args.gen;
        state->flags            = args.flags;

        if (args.dict.dict_len) {
                dict = dict_new ();
                buf = memdup (args.dict.dict_val, args.dict.dict_len);
                GF_VALIDATE_OR_GOTO (conn->bound_xl->name, buf, out);

                ret = dict_unserialize (buf, args.dict.dict_len, &dict);
                if (ret < 0) {
                        gf_log (conn->bound_xl->name, GF_LOG_ERROR,
                                "%"PRId64": %s (%"PRId64"): failed to "
                                "unserialize request buffer to dictionary",
                                frame->root->unique, state->loc.path,
                                state->resolve.ino);
                        goto err;
                }

                dict->extra_free = buf;
                buf = NULL;

                state->dict = dict;
        }

        resolve_and_resume (frame, server_setxattr_resume);

        return 0;
err:
        if (dict)
                dict_unref (dict);

        server_setxattr_cbk (frame, NULL, frame->this, -1, EINVAL);
out:
        if (buf)
                GF_FREE (buf);
        return 0;

}



int
server_fsetxattr (rpcsvc_request_t *req)
{
        server_state_t      *state                = NULL;
        dict_t              *dict                 = NULL;
        server_connection_t *conn                 = NULL;
        call_frame_t        *frame                = NULL;
        char                *buf                   = NULL;
        gfs3_fsetxattr_req   args                 = {0,};
        char                 dict_val[(16 *1024)] = {0,};
        int32_t              ret                  = -1;

        if (!req)
                return 0;

        conn = req->conn->trans->xl_private;

        args.dict.dict_val = dict_val;
        if (!xdr_to_fsetxattr_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_FSETXATTR;

        state = CALL_STATE (frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type      = RESOLVE_MUST;
        state->resolve.fd_no     = args.fd;
        state->flags             = args.flags;

        if (args.dict.dict_len) {
                dict = dict_new ();
                buf = memdup (args.dict.dict_val, args.dict.dict_len);
                GF_VALIDATE_OR_GOTO (conn->bound_xl->name, buf, out);

                ret = dict_unserialize (buf, args.dict.dict_len, &dict);
                if (ret < 0) {
                        gf_log (conn->bound_xl->name, GF_LOG_ERROR,
                                "%"PRId64": %s (%"PRId64"): failed to "
                                "unserialize request buffer to dictionary",
                                frame->root->unique, state->loc.path,
                                state->resolve.ino);
                        goto err;
                }
                dict->extra_free = buf;
                buf = NULL;
                state->dict = dict;
        }

        resolve_and_resume (frame, server_fsetxattr_resume);

        return 0;
err:
        if (dict)
                dict_unref (dict);

        server_setxattr_cbk (frame, NULL, frame->this, -1, EINVAL);
out:
        if (buf)
                GF_FREE (buf);
        return 0;
}



int
server_fxattrop (rpcsvc_request_t *req)
{
        dict_t              *dict                 = NULL;
        server_state_t      *state                = NULL;
        server_connection_t *conn                 = NULL;
        call_frame_t        *frame                = NULL;
        char                *buf                   = NULL;
        gfs3_fxattrop_req    args                 = {0,};
        char                 dict_val[(16 *1024)] = {0,};
        int32_t              ret                  = -1;

        if (!req)
                return 0;

        conn = req->conn->trans->xl_private;

        args.dict.dict_val = dict_val;
        if (!xdr_to_fxattrop_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_FXATTROP;

        state = CALL_STATE(frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type    = RESOLVE_MUST;
        state->resolve.fd_no   = args.fd;

        state->resolve.ino     = args.ino;
        state->resolve.gen     = args.gen;
        state->flags           = args.flags;

        if (args.dict.dict_len) {
                /* Unserialize the dictionary */
                dict = dict_new ();

                buf = memdup (args.dict.dict_val, args.dict.dict_len);
                GF_VALIDATE_OR_GOTO (conn->bound_xl->name, buf, out);

                ret = dict_unserialize (buf, args.dict.dict_len, &dict);
                if (ret < 0) {
                        gf_log (conn->bound_xl->name, GF_LOG_ERROR,
                                "fd - %"PRId64" (%"PRId64"): failed to unserialize "
                                "request buffer to dictionary",
                                state->resolve.fd_no, state->fd->inode->ino);
                        goto fail;
                }
                dict->extra_free = buf;
                buf = NULL;

                state->dict = dict;
        }

        resolve_and_resume (frame, server_fxattrop_resume);

        return 0;

fail:
        if (dict)
                dict_unref (dict);

        server_fxattrop_cbk (frame, NULL, frame->this, -1, EINVAL, NULL);
out:
        return 0;
}



int
server_xattrop (rpcsvc_request_t *req)
{
        dict_t              *dict                  = NULL;
        server_state_t      *state                 = NULL;
        server_connection_t *conn                  = NULL;
        call_frame_t        *frame                 = NULL;
        char                *buf                   = NULL;
        gfs3_xattrop_req     args                  = {0,};
        char                 dict_val[(16 *1024)]  = {0,};
        char                 path[SERVER_PATH_MAX] = {0,};
        int32_t              ret                   = -1;

        if (!req)
                return 0;

        conn = req->conn->trans->xl_private;
        args.dict.dict_val = dict_val;
        args.path = path;

        if (!xdr_to_xattrop_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_XATTROP;

        state = CALL_STATE(frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type    = RESOLVE_MUST;
        state->resolve.path    = gf_strdup (args.path);
        state->resolve.ino     = args.ino;
        state->resolve.gen     = args.gen;
        state->flags           = args.flags;

        if (args.dict.dict_len) {
                /* Unserialize the dictionary */
                dict = dict_new ();

                buf = memdup (args.dict.dict_val, args.dict.dict_len);
                GF_VALIDATE_OR_GOTO (conn->bound_xl->name, buf, out);

                ret = dict_unserialize (buf, args.dict.dict_len, &dict);
                if (ret < 0) {
                        gf_log (conn->bound_xl->name, GF_LOG_ERROR,
                                "fd - %"PRId64" (%"PRId64"): failed to unserialize "
                                "request buffer to dictionary",
                                state->resolve.fd_no, state->fd->inode->ino);
                        goto fail;
                }
                dict->extra_free = buf;
                buf = NULL;

                state->dict = dict;
        }

        resolve_and_resume (frame, server_xattrop_resume);

        return 0;
fail:
        if (dict)
                dict_unref (dict);

        server_xattrop_cbk (frame, NULL, frame->this, -1, EINVAL, NULL);
out:
        return 0;
}


int
server_getxattr (rpcsvc_request_t *req)
{
        server_state_t      *state                 = NULL;
        call_frame_t        *frame                 = NULL;
        gfs3_getxattr_req    args                  = {0,};
        char                 path[SERVER_PATH_MAX] = {0,};
        char                 name[4096]            = {0,};

        if (!req)
                return 0;

        args.path = path;
        args.name = name;

        if (!xdr_to_getxattr_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_GETXATTR;

        state = CALL_STATE (frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type  = RESOLVE_MUST;
        state->resolve.path  = gf_strdup (args.path);
        state->resolve.ino   = args.ino;
        state->resolve.gen   = args.gen;

        if (args.namelen)
                state->name = gf_strdup (args.name);

        resolve_and_resume (frame, server_getxattr_resume);
out:
        return 0;
}


int
server_fgetxattr (rpcsvc_request_t *req)
{
        server_state_t      *state      = NULL;
        call_frame_t        *frame      = NULL;
        gfs3_fgetxattr_req   args       = {0,};
        char                 name[4096] = {0,};

        if (!req)
                return 0;

        args.name = name;
        if (!xdr_to_fgetxattr_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_FGETXATTR;

        state = CALL_STATE (frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type  = RESOLVE_MUST;
        state->resolve.fd_no = args.fd;

        if (args.namelen)
                state->name = gf_strdup (args.name);

        resolve_and_resume (frame, server_fgetxattr_resume);
out:
        return 0;
}



int
server_removexattr (rpcsvc_request_t *req)
{
        server_state_t       *state                 = NULL;
        call_frame_t         *frame                 = NULL;
        gfs3_removexattr_req  args                  = {0,};
        char                  path[SERVER_PATH_MAX] = {0,};
        char                  name[4096]            = {0,};

        if (!req)
                return 0;

        args.path = path;
        args.name = name;
        if (!xdr_to_removexattr_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_REMOVEXATTR;

        state = CALL_STATE (frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type   = RESOLVE_MUST;
        state->resolve.path   = gf_strdup (args.path);
        state->resolve.ino    = args.ino;
        state->resolve.gen    = args.gen;
        state->name           = gf_strdup (args.name);

        resolve_and_resume (frame, server_removexattr_resume);
out:
        return 0;
}




int
server_opendir (rpcsvc_request_t *req)
{
        server_state_t   *state                 = NULL;
        call_frame_t     *frame                 = NULL;
        gfs3_opendir_req  args                  = {0,};
        char              path[SERVER_PATH_MAX] = {0,};

        if (!req)
                return 0;

        args.path = path;

        if (!xdr_to_opendir_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_OPENDIR;

        state = CALL_STATE (frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type   = RESOLVE_MUST;
        state->resolve.path   = gf_strdup (args.path);
        state->resolve.ino    = args.ino;
        state->resolve.gen    = args.gen;

        resolve_and_resume (frame, server_opendir_resume);
out:
        return 0;
}


int
server_readdirp (rpcsvc_request_t *req)
{
        server_state_t      *state = NULL;
        call_frame_t        *frame = NULL;
        gfs3_readdirp_req    args  = {0,};

        if (!req)
                return 0;

        if (!xdr_to_readdirp_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_READDIRP;

        state = CALL_STATE(frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type = RESOLVE_MUST;
        state->resolve.fd_no = args.fd;
        state->size   = args.size;
        state->offset = args.offset;

        resolve_and_resume (frame, server_readdirp_resume);
out:
        return 0;
}

int
server_readdir (rpcsvc_request_t *req)
{
        server_state_t      *state = NULL;
        call_frame_t        *frame = NULL;
        gfs3_readdir_req     args  = {0,};

        if (!req)
                return 0;

        if (!xdr_to_readdir_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_READDIR;

        state = CALL_STATE(frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type = RESOLVE_MUST;
        state->resolve.fd_no = args.fd;
        state->size   = args.size;
        state->offset = args.offset;

        resolve_and_resume (frame, server_readdir_resume);
out:
        return 0;
}

int
server_fsyncdir (rpcsvc_request_t *req)
{
        server_state_t      *state = NULL;
        call_frame_t        *frame = NULL;
        gfs3_fsyncdir_req    args  = {0,};

        if (!req)
                return 0;

        if (!xdr_to_fsyncdir_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_FSYNCDIR;

        state = CALL_STATE(frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type = RESOLVE_MUST;
        state->resolve.fd_no = args.fd;
        state->flags = args.data;

        resolve_and_resume (frame, server_fsyncdir_resume);
out:
        return 0;
}



int
server_mknod (rpcsvc_request_t *req)
{
        server_state_t      *state                  = NULL;
        call_frame_t        *frame                  = NULL;
        gfs3_mknod_req       args                   = {0,};
        char                 bname[SERVER_PATH_MAX] = {0,};
        char                 path[SERVER_PATH_MAX]  = {0,};

        if (!req)
                return 0;

        args.path  = path;
        args.bname = bname;

        if (!xdr_to_mknod_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_MKNOD;

        state = CALL_STATE (frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type    = RESOLVE_NOT;
        state->resolve.par     = args.par;
        state->resolve.gen     = args.gen;
        state->resolve.path    = gf_strdup (args.path);
        state->resolve.bname   = gf_strdup (args.bname);

        state->mode = args.mode;
        state->dev  = args.dev;

        resolve_and_resume (frame, server_mknod_resume);
out:
        return 0;
}


int
server_mkdir (rpcsvc_request_t *req)
{
        server_state_t      *state                  = NULL;
        call_frame_t        *frame                  = NULL;
        gfs3_mkdir_req       args                   = {0,};
        char                 bname[SERVER_PATH_MAX] = {0,};
        char                 path[SERVER_PATH_MAX]  = {0,};

        if (!req)
                return 0;

        args.path  = path;
        args.bname = bname;

        if (!xdr_to_mkdir_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_MKDIR;

        state = CALL_STATE (frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type    = RESOLVE_NOT;
        state->resolve.par     = args.par;
        state->resolve.gen     = args.gen;
        state->resolve.path    = gf_strdup (args.path);
        state->resolve.bname   = gf_strdup (args.bname);

        state->mode = args.mode;

        resolve_and_resume (frame, server_mkdir_resume);
out:
        return 0;
}


int
server_rmdir (rpcsvc_request_t *req)
{
        server_state_t      *state                  = NULL;
        call_frame_t        *frame                  = NULL;
        gfs3_rmdir_req       args                   = {0,};
        char                 bname[SERVER_PATH_MAX] = {0,};
        char                 path[SERVER_PATH_MAX]  = {0,};

        if (!req)
                return 0;

        args.path = path;
        args.bname = bname;

        if (!xdr_to_rmdir_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_RMDIR;

        state = CALL_STATE (frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type    = RESOLVE_MUST;
        state->resolve.par     = args.par;
        state->resolve.gen     = args.gen;
        state->resolve.path    = gf_strdup (args.path);
        state->resolve.bname   = gf_strdup (args.bname);

        resolve_and_resume (frame, server_rmdir_resume);
out:
        return 0;
}



int
server_inodelk (rpcsvc_request_t *req)
{
        server_state_t      *state                 = NULL;
        call_frame_t        *frame                 = NULL;
        gfs3_inodelk_req     args                  = {0,};
        char                 path[SERVER_PATH_MAX] = {0,};
        char                 volume[4096]          = {0,};
        int                  cmd                   = 0;

        if (!req)
                return 0;

        args.path = path;
        args.volume = volume;

        if (!xdr_to_inodelk_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_INODELK;

        state = CALL_STATE (frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type    = RESOLVE_EXACT;
        state->resolve.ino     = args.ino;
        state->resolve.gen     = args.gen;
        state->resolve.path    = gf_strdup (args.path);

        cmd = args.cmd;
        switch (cmd) {
        case GF_LK_GETLK:
                state->cmd = F_GETLK;
                break;
        case GF_LK_SETLK:
                state->cmd = F_SETLK;
                break;
        case GF_LK_SETLKW:
                state->cmd = F_SETLKW;
                break;
        }

        state->type = args.type;
        state->volume = gf_strdup (args.volume);

        gf_flock_to_flock (&args.flock, &state->flock);

        switch (state->type) {
        case GF_LK_F_RDLCK:
                state->flock.l_type = F_RDLCK;
                break;
        case GF_LK_F_WRLCK:
                state->flock.l_type = F_WRLCK;
                break;
        case GF_LK_F_UNLCK:
                state->flock.l_type = F_UNLCK;
                break;
        }

        resolve_and_resume (frame, server_inodelk_resume);
out:
        return 0;
}

int
server_finodelk (rpcsvc_request_t *req)
{
        server_state_t      *state        = NULL;
        call_frame_t        *frame        = NULL;
        gfs3_finodelk_req    args         = {0,};
        char                 volume[4096] = {0,};

        if (!req)
                return 0;

        args.volume = volume;
        if (!xdr_to_finodelk_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_FINODELK;

        state = CALL_STATE(frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type = RESOLVE_EXACT;
        state->volume = gf_strdup (args.volume);
        state->resolve.fd_no = args.fd;
        state->cmd = args.cmd;

        switch (state->cmd) {
        case GF_LK_GETLK:
                state->cmd = F_GETLK;
                break;
        case GF_LK_SETLK:
                state->cmd = F_SETLK;
                break;
        case GF_LK_SETLKW:
                state->cmd = F_SETLKW;
                break;
        }

        state->type = args.type;

        gf_flock_to_flock (&args.flock, &state->flock);

        switch (state->type) {
        case GF_LK_F_RDLCK:
                state->flock.l_type = F_RDLCK;
                break;
        case GF_LK_F_WRLCK:
                state->flock.l_type = F_WRLCK;
                break;
        case GF_LK_F_UNLCK:
                state->flock.l_type = F_UNLCK;
                break;
        }

        resolve_and_resume (frame, server_finodelk_resume);
out:
        return 0;
}


int
server_entrylk (rpcsvc_request_t *req)
{
        server_state_t      *state                 = NULL;
        call_frame_t        *frame                 = NULL;
        gfs3_entrylk_req     args                  = {0,};
        char                 path[SERVER_PATH_MAX] = {0,};
        char                 name[4096]            = {0,};
        char                 volume[4096]          = {0,};

        if (!req)
                return 0;

        args.path = path;
        args.volume = volume;
        args.name = name;

        if (!xdr_to_entrylk_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_ENTRYLK;

        state = CALL_STATE (frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type   = RESOLVE_EXACT;
        state->resolve.path   = gf_strdup (args.path);
        state->resolve.ino    = args.ino;
        state->resolve.gen    = args.gen;

        if (args.namelen)
                state->name   = gf_strdup (args.name);
        state->volume         = gf_strdup (args.volume);

        state->cmd            = args.cmd;
        state->type           = args.type;

        resolve_and_resume (frame, server_entrylk_resume);
out:
        return 0;
}

int
server_fentrylk (rpcsvc_request_t *req)
{
        server_state_t      *state        = NULL;
        call_frame_t        *frame        = NULL;
        gfs3_fentrylk_req    args         = {0,};
        char                 name[4096]   = {0,};
        char                 volume[4096] = {0,};

        if (!req)
                return 0;

        args.name = name;
        args.volume = volume;
        if (!xdr_to_fentrylk_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_FENTRYLK;

        state = CALL_STATE(frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type = RESOLVE_EXACT;
        state->resolve.fd_no = args.fd;
        state->cmd  = args.cmd;
        state->type = args.type;

        if (args.namelen)
                state->name = gf_strdup (args.name);
        state->volume = gf_strdup (args.volume);

        resolve_and_resume (frame, server_fentrylk_resume);
out:
        return 0;
}

int
server_access (rpcsvc_request_t *req)
{
        server_state_t      *state                 = NULL;
        call_frame_t        *frame                 = NULL;
        gfs3_access_req      args                  = {0,};
        char                 path[SERVER_PATH_MAX] = {0,};

        if (!req)
                return 0;

        args.path = path;
        if (!xdr_to_access_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_ACCESS;

        state = CALL_STATE (frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type  = RESOLVE_MUST;
        state->resolve.ino   = args.ino;
        state->resolve.gen   = args.gen;
        state->resolve.path  = gf_strdup (args.path);
        state->mask          = args.mask;

        resolve_and_resume (frame, server_access_resume);
out:
        return 0;
}



int
server_symlink (rpcsvc_request_t *req)
{
        server_state_t      *state                 = NULL;
        call_frame_t        *frame                 = NULL;
        gfs3_symlink_req     args                  = {0,};
        char                 linkname[4096]        = {0,};
        char                 path[SERVER_PATH_MAX] = {0,};
        char                 bname[4096]           = {0,};

        if (!req)
                return 0;

        args.path = path;
        args.bname = bname;
        args.linkname = linkname;

        if (!xdr_to_symlink_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_SYMLINK;

        state = CALL_STATE (frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type   = RESOLVE_NOT;
        state->resolve.par    = args.par;
        state->resolve.gen    = args.gen;
        state->resolve.path   = gf_strdup (args.path);
        state->resolve.bname  = gf_strdup (args.bname);
        state->name           = gf_strdup (args.linkname);

        resolve_and_resume (frame, server_symlink_resume);
out:
        return 0;
}



int
server_link (rpcsvc_request_t *req)
{
        server_state_t      *state                     = NULL;
        call_frame_t        *frame                     = NULL;
        gfs3_link_req        args                      = {0,};
        char                 oldpath[SERVER_PATH_MAX]  = {0,};
        char                 newpath[SERVER_PATH_MAX]  = {0,};
        char                 newbname[SERVER_PATH_MAX] = {0,};

        if (!req)
                return 0;

        args.oldpath  = oldpath;
        args.newpath  = newpath;
        args.newbname = newbname;

        if (!xdr_to_link_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_LINK;

        state = CALL_STATE (frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type    = RESOLVE_MUST;
        state->resolve.path    = gf_strdup (args.oldpath);
        state->resolve.ino     = args.oldino;
        state->resolve.gen     = args.oldgen;

        state->resolve2.type   = RESOLVE_NOT;
        state->resolve2.path   = gf_strdup (args.newpath);
        state->resolve2.bname  = gf_strdup (args.newbname);
        state->resolve2.par    = args.newpar;
        state->resolve2.gen    = args.newgen;

        resolve_and_resume (frame, server_link_resume);
out:
        return 0;
}


int
server_rename (rpcsvc_request_t *req)
{
        server_state_t      *state                     = NULL;
        call_frame_t        *frame                     = NULL;
        gfs3_rename_req      args                      = {0,};
        char                 oldpath[SERVER_PATH_MAX]  = {0,};
        char                 oldbname[SERVER_PATH_MAX] = {0,};
        char                 newpath[SERVER_PATH_MAX]  = {0,};
        char                 newbname[SERVER_PATH_MAX] = {0,};

        if (!req)
                return 0;

        args.oldpath  = oldpath;
        args.oldbname = oldbname;
        args.newpath  = newpath;
        args.newbname = newbname;
        if (!xdr_to_rename_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_RENAME;

        state = CALL_STATE (frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type   = RESOLVE_MUST;
        state->resolve.path   = gf_strdup (args.oldpath);
        state->resolve.bname  = gf_strdup (args.oldbname);
        state->resolve.par    = args.oldpar;
        state->resolve.gen    = args.oldgen;

        state->resolve2.type  = RESOLVE_MAY;
        state->resolve2.path  = gf_strdup (args.newpath);
        state->resolve2.bname = gf_strdup (args.newbname);
        state->resolve2.par   = args.newpar;
        state->resolve2.gen   = args.newgen;

        resolve_and_resume (frame, server_rename_resume);
out:
        return 0;
}

int
server_lk (rpcsvc_request_t *req)
{
        server_state_t      *state = NULL;
        server_connection_t *conn  = NULL;
        call_frame_t        *frame = NULL;
        gfs3_lk_req          args  = {0,};

        if (!req)
                return 0;

        conn = req->conn->trans->xl_private;

        if (!xdr_to_lk_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_LK;

        state = CALL_STATE (frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.fd_no = args.fd;
        state->cmd =  args.cmd;
        state->type = args.type;

        switch (state->cmd) {
        case GF_LK_GETLK:
                state->cmd = F_GETLK;
                break;
        case GF_LK_SETLK:
                state->cmd = F_SETLK;
                break;
        case GF_LK_SETLKW:
                state->cmd = F_SETLKW;
                break;
        }

        gf_flock_to_flock (&args.flock, &state->flock);

        switch (state->type) {
        case GF_LK_F_RDLCK:
                state->flock.l_type = F_RDLCK;
                break;
        case GF_LK_F_WRLCK:
                state->flock.l_type = F_WRLCK;
                break;
        case GF_LK_F_UNLCK:
                state->flock.l_type = F_UNLCK;
                break;
        default:
                gf_log (conn->bound_xl->name, GF_LOG_ERROR,
                        "fd - %"PRId64" (%"PRId64"): Unknown lock type: %"PRId32"!",
                        state->resolve.fd_no, state->fd->inode->ino, state->type);
                break;
        }


        resolve_and_resume (frame, server_lk_resume);
out:
        return 0;
}


int
server_rchecksum (rpcsvc_request_t *req)
{
        server_state_t      *state = NULL;
        call_frame_t        *frame = NULL;
        gfs3_rchecksum_req   args  = {0,};

        if (!req)
                return 0;

        if (!xdr_to_rchecksum_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_RCHECKSUM;

        state = CALL_STATE(frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type  = RESOLVE_MAY;
        state->resolve.fd_no = args.fd;
        state->offset        = args.offset;
        state->size          = args.len;

        resolve_and_resume (frame, server_rchecksum_resume);
out:
        return 0;
}

int
server_null (rpcsvc_request_t *req)
{
        gf_common_rsp rsp = {0,};

        rsp.gfs_id = req->gfs_id;
        /* Accepted */
        rsp.op_ret = 0;

        server_submit_reply (NULL, req, &rsp, NULL, 0, NULL,
                             (gfs_serialize_t)xdr_serialize_common_rsp);

        return 0;
}

int
server_lookup (rpcsvc_request_t *req)
{
        call_frame_t        *frame                  = NULL;
        server_connection_t *conn                   = NULL;
        server_state_t      *state                  = NULL;
        dict_t              *xattr_req              = NULL;
        char                *buf                    = NULL;
        gfs3_lookup_req      args                   = {0,};
        int                  ret                    = 0;
        char                 path[SERVER_PATH_MAX]  = {0,};
        char                 bname[SERVER_PATH_MAX] = {0,};
        char                 dict_val[(16 * 1024)]  = {0,};

        if (!req)
                return 0;

        conn = req->conn->trans->xl_private;

        args.path = path;
        args.bname = bname;
        args.dict.dict_val = dict_val;

        if (!xdr_to_lookup_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto err;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS;
                goto err;
        }
        frame->root->op = GF_FOP_LOOKUP;

        /* NOTE: lookup() uses req->ino only to identify if a lookup()
         *       is requested for 'root' or not
         */

        state = CALL_STATE (frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }
        state->resolve.ino    = args.ino;
        if (state->resolve.ino != 1)
                state->resolve.ino = 0;

        state->resolve.type   = RESOLVE_DONTCARE;
        state->resolve.par    = args.par;
        state->resolve.gen    = args.gen;
        state->resolve.path   = gf_strdup (args.path);

        if (IS_NOT_ROOT (STRLEN_0 (args.path))) {
                state->resolve.bname = gf_strdup (args.bname);
        }

        if (args.dict.dict_len) {
                /* Unserialize the dictionary */
                xattr_req = dict_new ();

                buf = memdup (args.dict.dict_val, args.dict.dict_len);
                if (buf == NULL) {
                        gf_log (conn->bound_xl->name, GF_LOG_ERROR,
                                "out of memory");
                        goto out;
                }

                ret = dict_unserialize (buf, args.dict.dict_len,
                                        &xattr_req);
                if (ret < 0) {
                        gf_log (conn->bound_xl->name, GF_LOG_ERROR,
                                "%"PRId64": %s (%"PRId64"): failed to "
                                "unserialize req-buffer to dictionary",
                                frame->root->unique, state->resolve.path,
                                state->resolve.ino);
                        goto out;
                }

                state->dict = xattr_req;

                xattr_req->extra_free = buf;

                buf = NULL;
        }

        resolve_and_resume (frame, server_lookup_resume);

        return 0;
out:
        if (xattr_req)
                dict_unref (xattr_req);

        if (buf) {
                GF_FREE (buf);
        }

        server_lookup_cbk (frame, NULL, frame->this, -1, EINVAL, NULL, NULL,
                           NULL, NULL);
err:
        return 0;
}

int
server_statfs (rpcsvc_request_t *req)
{
        server_state_t      *state = NULL;
        call_frame_t        *frame = NULL;
        gfs3_statfs_req      args  = {0,};
        char                 path[SERVER_PATH_MAX]  = {0,};

        if (!req)
                return 0;

        args.path = path;
        if (!xdr_to_statfs_req (req->msg[0], &args)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        frame = get_frame_from_request (req);
        if (!frame) {
                // something wrong, mostly insufficient memory
                req->rpc_err = GARBAGE_ARGS; /* TODO */
                goto out;
        }
        frame->root->op = GF_FOP_STATFS;

        state = CALL_STATE (frame);
        if (!state->conn->bound_xl) {
                /* auth failure, request on subvolume without setvolume */
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        state->resolve.type   = RESOLVE_MUST;
        state->resolve.ino    = args.ino;
        if (!state->resolve.ino)
                state->resolve.ino = 1;
        state->resolve.gen    = args.gen;
        state->resolve.path   = gf_strdup (args.path);

        resolve_and_resume (frame, server_statfs_resume);
out:
        return 0;
}


rpcsvc_actor_t glusterfs3_1_fop_actors[] = {
        [GFS3_OP_NULL]        = { "NULL",       GFS3_OP_NULL, server_null, NULL, NULL},
        [GFS3_OP_STAT]        = { "STAT",       GFS3_OP_STAT, server_stat, NULL, NULL },
        [GFS3_OP_READLINK]    = { "READLINK",   GFS3_OP_READLINK, server_readlink, NULL, NULL },
        [GFS3_OP_MKNOD]       = { "MKNOD",      GFS3_OP_MKNOD, server_mknod, NULL, NULL },
        [GFS3_OP_MKDIR]       = { "MKDIR",      GFS3_OP_MKDIR, server_mkdir, NULL, NULL },
        [GFS3_OP_UNLINK]      = { "UNLINK",     GFS3_OP_UNLINK, server_unlink, NULL, NULL },
        [GFS3_OP_RMDIR]       = { "RMDIR",      GFS3_OP_RMDIR, server_rmdir, NULL, NULL },
        [GFS3_OP_SYMLINK]     = { "SYMLINK",    GFS3_OP_SYMLINK, server_symlink, NULL, NULL },
        [GFS3_OP_RENAME]      = { "RENAME",     GFS3_OP_RENAME, server_rename, NULL, NULL },
        [GFS3_OP_LINK]        = { "LINK",       GFS3_OP_LINK, server_link, NULL, NULL },
        [GFS3_OP_TRUNCATE]    = { "TRUNCATE",   GFS3_OP_TRUNCATE, server_truncate, NULL, NULL },
        [GFS3_OP_OPEN]        = { "OPEN",       GFS3_OP_OPEN, server_open, NULL, NULL },
        [GFS3_OP_READ]        = { "READ",       GFS3_OP_READ, server_readv, NULL, NULL },
        [GFS3_OP_WRITE]       = { "WRITE",      GFS3_OP_WRITE, server_writev, server_writev_vec, NULL },
        [GFS3_OP_STATFS]      = { "STATFS",     GFS3_OP_STATFS, server_statfs, NULL, NULL },
        [GFS3_OP_FLUSH]       = { "FLUSH",      GFS3_OP_FLUSH, server_flush, NULL, NULL },
        [GFS3_OP_FSYNC]       = { "FSYNC",      GFS3_OP_FSYNC, server_fsync, NULL, NULL },
        [GFS3_OP_SETXATTR]    = { "SETXATTR",   GFS3_OP_SETXATTR, server_setxattr, NULL, NULL },
        [GFS3_OP_GETXATTR]    = { "GETXATTR",   GFS3_OP_GETXATTR, server_getxattr, NULL, NULL },
        [GFS3_OP_REMOVEXATTR] = { "REMOVEXATTR", GFS3_OP_REMOVEXATTR, server_removexattr, NULL, NULL },
        [GFS3_OP_OPENDIR]     = { "OPENDIR",    GFS3_OP_OPENDIR, server_opendir, NULL, NULL },
        [GFS3_OP_FSYNCDIR]    = { "FSYNCDIR",   GFS3_OP_FSYNCDIR, server_fsyncdir, NULL, NULL },
        [GFS3_OP_ACCESS]      = { "ACCESS",     GFS3_OP_ACCESS, server_access, NULL, NULL },
        [GFS3_OP_CREATE]      = { "CREATE",     GFS3_OP_CREATE, server_create, NULL, NULL },
        [GFS3_OP_FTRUNCATE]   = { "FTRUNCATE",  GFS3_OP_FTRUNCATE, server_ftruncate, NULL, NULL },
        [GFS3_OP_FSTAT]       = { "FSTAT",      GFS3_OP_FSTAT, server_fstat, NULL, NULL },
        [GFS3_OP_LK]          = { "LK",         GFS3_OP_LK, server_lk, NULL, NULL },
        [GFS3_OP_LOOKUP]      = { "LOOKUP",     GFS3_OP_LOOKUP, server_lookup, NULL, NULL },
        [GFS3_OP_READDIR]     = { "READDIR",    GFS3_OP_READDIR, server_readdir, NULL, NULL },
        [GFS3_OP_INODELK]     = { "INODELK",    GFS3_OP_INODELK, server_inodelk, NULL, NULL },
        [GFS3_OP_FINODELK]    = { "FINODELK",   GFS3_OP_FINODELK, server_finodelk, NULL, NULL },
	[GFS3_OP_ENTRYLK]     = { "ENTRYLK",    GFS3_OP_ENTRYLK, server_entrylk, NULL, NULL },
	[GFS3_OP_FENTRYLK]    = { "FENTRYLK",   GFS3_OP_FENTRYLK, server_fentrylk, NULL, NULL },
        [GFS3_OP_XATTROP]     = { "XATTROP",    GFS3_OP_XATTROP, server_xattrop, NULL, NULL },
        [GFS3_OP_FXATTROP]    = { "FXATTROP",   GFS3_OP_FXATTROP, server_fxattrop, NULL, NULL },
        [GFS3_OP_FGETXATTR]   = { "FGETXATTR",  GFS3_OP_FGETXATTR, server_fgetxattr, NULL, NULL },
        [GFS3_OP_FSETXATTR]   = { "FSETXATTR",  GFS3_OP_FSETXATTR, server_fsetxattr, NULL, NULL },
        [GFS3_OP_RCHECKSUM]   = { "RCHECKSUM",  GFS3_OP_RCHECKSUM, server_rchecksum, NULL, NULL },
        [GFS3_OP_SETATTR]     = { "SETATTR",    GFS3_OP_SETATTR, server_setattr, NULL, NULL },
        [GFS3_OP_FSETATTR]    = { "FSETATTR",   GFS3_OP_FSETATTR, server_fsetattr, NULL, NULL },
        [GFS3_OP_READDIRP]    = { "READDIRP",   GFS3_OP_READDIRP, server_readdirp, NULL, NULL },
        [GFS3_OP_RELEASE]     = { "RELEASE",    GFS3_OP_RELEASE, server_release, NULL, NULL },
        [GFS3_OP_RELEASEDIR]  = { "RELEASEDIR", GFS3_OP_RELEASEDIR, server_releasedir, NULL, NULL },
};


struct rpcsvc_program glusterfs3_1_fop_prog = {
        .progname  = "GlusterFS-3.1.0",
        .prognum   = GLUSTER3_1_FOP_PROGRAM,
        .progver   = GLUSTER3_1_FOP_VERSION,
        .numactors = GLUSTER3_1_FOP_PROCCNT,
        .actors    = glusterfs3_1_fop_actors,
        .progport  = 7007,
};
