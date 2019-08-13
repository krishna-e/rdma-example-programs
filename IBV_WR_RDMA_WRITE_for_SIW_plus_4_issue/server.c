/* build:
 * cc -g server.c  -lcxgb4 -libverbs -lrdmacm  -o server
 * 
 * server <integer data to write into server's memory>
 */ 
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <rdma/rdma_cma.h>

enum   { 
        RESOLVE_TIMEOUT_MS = 5000, 
}; 
struct pdata { 
        uint64_t	buf_va; 
        uint32_t	buf_rkey;
}; 

int main(int argc, char   *argv[ ]) 
{
	struct pdata				client_pdata;

	struct rdma_event_channel		*cm_channel; 
	struct rdma_cm_id				*cm_id; 
	struct rdma_cm_event				*event;  
	struct rdma_conn_param			conn_param = { };

	struct ibv_pd					*pd; 
	struct ibv_comp_channel			*comp_chan; 
	struct ibv_cq					*cq; 
	struct ibv_mr					*mr; 
	struct ibv_qp_init_attr			qp_attr = { }; 
	struct ibv_sge					sge; 
	struct ibv_send_wr				send_wr = { }; 
	struct ibv_send_wr 				*bad_send_wr; 
	uint32_t						*buf; 
	int							err;

	cm_channel = rdma_create_event_channel(); 
	if (!cm_channel)  
		return 1; 

	struct sockaddr_in      sin;
	struct rdma_cm_id           *listen_id;

	err = rdma_create_id(cm_channel, &listen_id, NULL, RDMA_PS_TCP);
	if (err)
		return err;

	sin.sin_family = AF_INET;
	sin.sin_port = htons(20079);
	sin.sin_addr.s_addr = INADDR_ANY;


	/* Bind to local port and listen for connection request */

	err = rdma_bind_addr(listen_id, (struct sockaddr *) &sin);
	if (err)
		return 1;

	err = rdma_listen(listen_id, 1);
	if (err)
		return 1;

	err = rdma_get_cm_event(cm_channel, &event);
	if (err)
		return err;

	if (event->event != RDMA_CM_EVENT_CONNECT_REQUEST)
		return 1;

	cm_id = event->id;

	memcpy(&client_pdata, event->param.conn.private_data, sizeof client_pdata);

	rdma_ack_cm_event(event);

	/* Create verbs objects now that we know which device to use */

	pd = ibv_alloc_pd(cm_id->verbs);
	if (!pd)
		return 1;

	comp_chan = ibv_create_comp_channel(cm_id->verbs);
	if (!comp_chan)
		return 1;

	cq = ibv_create_cq(cm_id->verbs, 2, NULL, comp_chan, 0);
	if (!cq)
		return 1;

	if (ibv_req_notify_cq(cq, 0))
		return 1;

	buf = calloc(2, sizeof (uint32_t));
	if (!buf)
		return 1;

	mr = ibv_reg_mr(pd, buf, 2 * sizeof (uint32_t),
			IBV_ACCESS_LOCAL_WRITE |
			IBV_ACCESS_REMOTE_READ |
			IBV_ACCESS_REMOTE_WRITE);
	if (!mr)
		return 1;

	qp_attr.cap.max_send_wr = 1;
	qp_attr.cap.max_send_sge = 1;
	qp_attr.cap.max_recv_wr = 1;
	qp_attr.cap.max_recv_sge = 1;

	qp_attr.send_cq = cq;
	qp_attr.recv_cq = cq;

	qp_attr.qp_type = IBV_QPT_RC;

	err = rdma_create_qp(cm_id, pd, &qp_attr);
	if (err)
		return err;

        conn_param.private_data = NULL;
        conn_param.private_data_len = 0;

	/* Accept connection */
	err = rdma_accept(cm_id, &conn_param);
	if (err)
		return 1;

	err = rdma_get_cm_event(cm_channel, &event);
	if (err)
		return err;

	if (event->event != RDMA_CM_EVENT_ESTABLISHED)
		return 1;


	rdma_ack_cm_event(event);


	buf[0] = strtoul(argv[1], NULL, 0);
	printf("RDMA WRITE operation with data: %d\n", buf[0]);
	buf[0] = htonl(buf[0]);

	sge.addr = (uint64_t) buf; 
	sge.length = sizeof (uint32_t);
	sge.lkey = mr->lkey;

	send_wr.wr_id                 = 1;
	send_wr.opcode                = IBV_WR_RDMA_WRITE;
	send_wr.sg_list               = &sge;
	send_wr.num_sge               = 1;
	send_wr.wr.rdma.rkey          = be32toh(client_pdata.buf_rkey);
	send_wr.wr.rdma.remote_addr   = be64toh(client_pdata.buf_va);

	if (ibv_post_send(cm_id->qp, &send_wr, &bad_send_wr))
		return 1;
	return 0;
}
