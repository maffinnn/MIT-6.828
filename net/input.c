#include "ns.h"
#include <kern/e1000.h>

#define MAXSTORAGE 8
extern union Nsipc nsipcbuf;
static struct jif_pkt *pkt = (struct jif_pkt *)REQVA;


// input helper environment ͨ������ϵͳ���ô������������ȡ���ݰ�,
// ����ֵ��ע���һ���� �п����հ���sys_netpacket_recv��̫��, 
// ���͸�������ʱ ���������ܶ�ȡ�ٶ����� ������Ӧ�����ݱ���ˢ, ����������һ����ʱ�洢
// ���յ������ݱ�����input helper environment��
void
input(envid_t ns_envid)
{
	binaryname = "ns_input";
	// LAB 6: Your code here:
	// 	- read a packet from the device driver
	//	- send it to the network server
	// Hint: When you IPC a page to the network server, it will be
	// reading from it for a while, so don't immediately receive
	// another packet in to the same physical page.
	int i, r;
	int len;
	struct jif_pkt *cpkt = pkt;
	
	for(i = 0; i < 10; i++)
		if ((r = sys_page_alloc(0, (void*)((uintptr_t)pkt + i * PGSIZE), PTE_P | PTE_U | PTE_W)) < 0)
			panic("sys_page_alloc: %e", r);
	
	i = 0;
	while(1) {
		while((len = sys_netpacket_recv((void*)((uintptr_t)cpkt + sizeof(cpkt->jp_len)), PGSIZE - sizeof(cpkt->jp_len))) < 0) {
			sys_yield();
		}

		cpkt->jp_len = len;
		ipc_send(ns_envid, NSREQ_INPUT, cpkt, PTE_P | PTE_U);
		i = (i + 1) % 10;
		cpkt = (struct jif_pkt*)((uintptr_t)pkt + i * PGSIZE);
		sys_yield();
	}	

	
}
