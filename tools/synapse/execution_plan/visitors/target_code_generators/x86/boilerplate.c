{{global_state}}

bool nf_init() {
  {{nf_init}}
}

int nf_process(uint16_t device, uint8_t **p, uint16_t pkt_len, int64_t now, struct rte_mbuf *mbuf) {
  {{nf_process}}
}
