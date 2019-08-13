/*
 * build:
 * cc -g  client.c  -lcxgb4 -libverbs -lrdmacm  -o client
 * 
 * usage: 
 * client <server's ipaddr>
 * 
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <rdma/rdma_cma.h>

enum { 
	RESOLVE_TIMEOUT_MS = 5000,
};

struct pdata { 
	uint64_t buf_va; 
	uint32_t buf_rkey; 
};

int main(int argc, char *argv[]) 
{ 
	struct pdata    pdata;
	struct rdma_event_channel   *cm_channel;
	struct rdma_cm_id           *cm_id; 
	struct rdma_cm_event        *event; 
	struct rdma_conn_param      conn_param = { };

	struct ibv_pd           *pd; 
	struct ibv_comp_channel *comp_chan; 
	struct ibv_cq           *cq;  
	struct ibv_mr           *mr; 
	struct ibv_qp_init_attr qp_attr = { };


	uint32_t                *buf;

	int                     err;
	/* Set up RDMA CM structures */

	cm_channel = rdma_create_event_channel();
	if (!cm_channel) 
		return 1;
	struct addrinfo                                 *res, *t;
	int n;

	struct addrinfo                                 hints = {
		.ai_family    = AF_INET,
		.ai_socktype  = SOCK_STREAM
	};



	err = rdma_create_id(cm_channel, &cm_id, NULL, RDMA_PS_TCP);
	if (err)
		return err;

	n = getaddrinfo(argv[1], "20079", &hints, &res);
	if (n < 0)
		return 1;


	for (t = res; t; t = t->ai_next) {
		err = rdma_resolve_addr(cm_id, NULL, t->ai_addr, RESOLVE_TIMEOUT_MS);
		if (!err)
			break;
	}
	if (err)
		return err;

	err = rdma_get_cm_event(cm_channel, &event);
	if (err)
		return err;

	if (event->event != RDMA_CM_EVENT_ADDR_RESOLVED)
		return 1;

	rdma_ack_cm_event(event);

	err = rdma_resolve_route(cm_id, RESOLVE_TIMEOUT_MS);
	if (err)
		return err;

	err = rdma_get_cm_event(cm_channel, &event);
	if (err)
		return err;

	if (event->event != RDMA_CM_EVENT_ROUTE_RESOLVED)
		return 1;

	rdma_ack_cm_event(event);


	pd = ibv_alloc_pd(cm_id->verbs);
	if (!pd)
		return 1;

	comp_chan = ibv_create_comp_channel(cm_id->verbs);
	if (!comp_chan)
		return 1;
	cq = ibv_create_cq(cm_id->verbs, 2,NULL, comp_chan, 0);
	if (!cq)
		return 1;

	if (ibv_req_notify_cq(cq, 0))
		return 1;

	buf = calloc(2, sizeof (uint32_t));
	if (!buf)
		return 1;

	mr = ibv_reg_mr(pd, buf,2 * sizeof(uint32_t),
			IBV_ACCESS_LOCAL_WRITE |
			IBV_ACCESS_REMOTE_READ |
			IBV_ACCESS_REMOTE_WRITE);
	if (!mr)
		return 1;

	qp_attr.cap.max_send_wr = 2;
	qp_attr.cap.max_send_sge = 1;
	qp_attr.cap.max_recv_wr = 1;
	qp_attr.cap.max_recv_sge = 1;

	qp_attr.send_cq        = cq;
	qp_attr.recv_cq        = cq;
	qp_attr.qp_type        = IBV_QPT_RC;

	err = rdma_create_qp(cm_id, pd, &qp_attr);
	if (err)
		return err;

	pdata.buf_va = be64toh((unsigned long long) buf); 
	pdata.buf_rkey = be32toh(mr->rkey); 

	conn_param.initiator_depth = 1;
	conn_param.private_data = &pdata; 
	conn_param.private_data_len = sizeof pdata;

	err = rdma_connect(cm_id, &conn_param);
	if (err) {
		printf("Connect error: %d\n",err);
		return err;
	}

	err = rdma_get_cm_event(cm_channel,&event);
	if (err) {
		printf("Get cm event error: %d\n",err);
		return err;
	}

	if (event->event != RDMA_CM_EVENT_ESTABLISHED) {
		printf("Unexpected event : %s\n",rdma_event_str(event->event));
		return 1;
	}


	rdma_ack_cm_event(event);

	while (1) {
		printf("content of buf[0] = %d\n", ntohl(buf[0]));
		sleep(1);
	}

	return 0;
}

