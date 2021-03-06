/*
 * Please do not edit this file.
 * It was generated using rpcgen.
 */

#include "glusterd1.h"

bool_t
xdr_glusterd_volume_status (XDR *xdrs, glusterd_volume_status *objp)
{

	 if (!xdr_enum (xdrs, (enum_t *) objp))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_gd1_mgmt_probe_req (XDR *xdrs, gd1_mgmt_probe_req *objp)
{

	 if (!xdr_vector (xdrs, (char *)objp->uuid, 16,
		sizeof (u_char), (xdrproc_t) xdr_u_char))
		 return FALSE;
	 if (!xdr_string (xdrs, &objp->hostname, ~0))
		 return FALSE;
	 if (!xdr_int (xdrs, &objp->port))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_gd1_mgmt_probe_rsp (XDR *xdrs, gd1_mgmt_probe_rsp *objp)
{

	 if (!xdr_vector (xdrs, (char *)objp->uuid, 16,
		sizeof (u_char), (xdrproc_t) xdr_u_char))
		 return FALSE;
	 if (!xdr_string (xdrs, &objp->hostname, ~0))
		 return FALSE;
	 if (!xdr_int (xdrs, &objp->port))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_gd1_mgmt_friend_req (XDR *xdrs, gd1_mgmt_friend_req *objp)
{

	 if (!xdr_vector (xdrs, (char *)objp->uuid, 16,
		sizeof (u_char), (xdrproc_t) xdr_u_char))
		 return FALSE;
	 if (!xdr_string (xdrs, &objp->hostname, ~0))
		 return FALSE;
	 if (!xdr_int (xdrs, &objp->port))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_gd1_mgmt_friend_rsp (XDR *xdrs, gd1_mgmt_friend_rsp *objp)
{

	 if (!xdr_vector (xdrs, (char *)objp->uuid, 16,
		sizeof (u_char), (xdrproc_t) xdr_u_char))
		 return FALSE;
	 if (!xdr_string (xdrs, &objp->hostname, ~0))
		 return FALSE;
	 if (!xdr_int (xdrs, &objp->port))
		 return FALSE;
	 if (!xdr_int (xdrs, &objp->op_ret))
		 return FALSE;
	 if (!xdr_int (xdrs, &objp->op_errno))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_gd1_mgmt_unfriend_req (XDR *xdrs, gd1_mgmt_unfriend_req *objp)
{

	 if (!xdr_vector (xdrs, (char *)objp->uuid, 16,
		sizeof (u_char), (xdrproc_t) xdr_u_char))
		 return FALSE;
	 if (!xdr_string (xdrs, &objp->hostname, ~0))
		 return FALSE;
	 if (!xdr_int (xdrs, &objp->port))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_gd1_mgmt_unfriend_rsp (XDR *xdrs, gd1_mgmt_unfriend_rsp *objp)
{

	 if (!xdr_vector (xdrs, (char *)objp->uuid, 16,
		sizeof (u_char), (xdrproc_t) xdr_u_char))
		 return FALSE;
	 if (!xdr_string (xdrs, &objp->hostname, ~0))
		 return FALSE;
	 if (!xdr_int (xdrs, &objp->port))
		 return FALSE;
	 if (!xdr_int (xdrs, &objp->op_ret))
		 return FALSE;
	 if (!xdr_int (xdrs, &objp->op_errno))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_gd1_mgmt_cluster_lock_req (XDR *xdrs, gd1_mgmt_cluster_lock_req *objp)
{

	 if (!xdr_vector (xdrs, (char *)objp->uuid, 16,
		sizeof (u_char), (xdrproc_t) xdr_u_char))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_gd1_mgmt_cluster_lock_rsp (XDR *xdrs, gd1_mgmt_cluster_lock_rsp *objp)
{

	 if (!xdr_vector (xdrs, (char *)objp->uuid, 16,
		sizeof (u_char), (xdrproc_t) xdr_u_char))
		 return FALSE;
	 if (!xdr_int (xdrs, &objp->op_ret))
		 return FALSE;
	 if (!xdr_int (xdrs, &objp->op_errno))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_gd1_mgmt_cluster_unlock_req (XDR *xdrs, gd1_mgmt_cluster_unlock_req *objp)
{

	 if (!xdr_vector (xdrs, (char *)objp->uuid, 16,
		sizeof (u_char), (xdrproc_t) xdr_u_char))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_gd1_mgmt_cluster_unlock_rsp (XDR *xdrs, gd1_mgmt_cluster_unlock_rsp *objp)
{

	 if (!xdr_vector (xdrs, (char *)objp->uuid, 16,
		sizeof (u_char), (xdrproc_t) xdr_u_char))
		 return FALSE;
	 if (!xdr_int (xdrs, &objp->op_ret))
		 return FALSE;
	 if (!xdr_int (xdrs, &objp->op_errno))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_gd1_mgmt_stage_op_req (XDR *xdrs, gd1_mgmt_stage_op_req *objp)
{

	 if (!xdr_vector (xdrs, (char *)objp->uuid, 16,
		sizeof (u_char), (xdrproc_t) xdr_u_char))
		 return FALSE;
	 if (!xdr_int (xdrs, &objp->op))
		 return FALSE;
	 if (!xdr_bytes (xdrs, (char **)&objp->buf.buf_val, (u_int *) &objp->buf.buf_len, ~0))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_gd1_mgmt_stage_op_rsp (XDR *xdrs, gd1_mgmt_stage_op_rsp *objp)
{

	 if (!xdr_vector (xdrs, (char *)objp->uuid, 16,
		sizeof (u_char), (xdrproc_t) xdr_u_char))
		 return FALSE;
	 if (!xdr_int (xdrs, &objp->op))
		 return FALSE;
	 if (!xdr_int (xdrs, &objp->op_ret))
		 return FALSE;
	 if (!xdr_int (xdrs, &objp->op_errno))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_gd1_mgmt_commit_op_req (XDR *xdrs, gd1_mgmt_commit_op_req *objp)
{

	 if (!xdr_vector (xdrs, (char *)objp->uuid, 16,
		sizeof (u_char), (xdrproc_t) xdr_u_char))
		 return FALSE;
	 if (!xdr_int (xdrs, &objp->op))
		 return FALSE;
	 if (!xdr_bytes (xdrs, (char **)&objp->buf.buf_val, (u_int *) &objp->buf.buf_len, ~0))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_gd1_mgmt_commit_op_rsp (XDR *xdrs, gd1_mgmt_commit_op_rsp *objp)
{

	 if (!xdr_vector (xdrs, (char *)objp->uuid, 16,
		sizeof (u_char), (xdrproc_t) xdr_u_char))
		 return FALSE;
	 if (!xdr_int (xdrs, &objp->op))
		 return FALSE;
	 if (!xdr_int (xdrs, &objp->op_ret))
		 return FALSE;
	 if (!xdr_int (xdrs, &objp->op_errno))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_gd1_mgmt_friend_update (XDR *xdrs, gd1_mgmt_friend_update *objp)
{

	 if (!xdr_vector (xdrs, (char *)objp->uuid, 16,
		sizeof (u_char), (xdrproc_t) xdr_u_char))
		 return FALSE;
	 if (!xdr_bytes (xdrs, (char **)&objp->friends.friends_val, (u_int *) &objp->friends.friends_len, ~0))
		 return FALSE;
	 if (!xdr_int (xdrs, &objp->port))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_gd1_mgmt_friend_update_rsp (XDR *xdrs, gd1_mgmt_friend_update_rsp *objp)
{

	 if (!xdr_vector (xdrs, (char *)objp->uuid, 16,
		sizeof (u_char), (xdrproc_t) xdr_u_char))
		 return FALSE;
	 if (!xdr_int (xdrs, &objp->op))
		 return FALSE;
	 if (!xdr_int (xdrs, &objp->op_ret))
		 return FALSE;
	 if (!xdr_int (xdrs, &objp->op_errno))
		 return FALSE;
	return TRUE;
}
