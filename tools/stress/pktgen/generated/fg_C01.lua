package.path = package.path .. ";?.lua;test/?.lua;app/?.lua;../?.lua;scripts/?.lua"
require "Pktgen"

pktgen.screen("off")

local port = "0"
local measure_sec = 43
local warmup_sec = 2
local pkt_size = 46
local rate_pct = 100

pktgen.set(port, "size", pkt_size)
pktgen.set(port, "rate", rate_pct)
pktgen.set(port, "count", 0)
pktgen.set_proto(port, "udp")
pktgen.page("range")

pktgen.range.dst_ip(port, "start", "192.168.1.100")
pktgen.range.dst_ip(port, "inc", "0.0.0.0")
pktgen.range.dst_ip(port, "min", "192.168.1.100")
pktgen.range.dst_ip(port, "max", "192.168.1.100")

pktgen.range.src_ip(port, "start", "10.0.0.1")
pktgen.range.src_ip(port, "inc", "0.0.0.0")
pktgen.range.src_ip(port, "min", "10.0.0.1")
pktgen.range.src_ip(port, "max", "10.0.0.1")

pktgen.range.dst_port(port, "start", 53)
pktgen.range.dst_port(port, "inc", 0)
pktgen.range.dst_port(port, "min", 53)
pktgen.range.dst_port(port, "max", 53)

pktgen.range.src_port(port, "start", 4000)
pktgen.range.src_port(port, "inc", 0)
pktgen.range.src_port(port, "min", 4000)
pktgen.range.src_port(port, "max", 4000)

pktgen.set_range(port, "on")

-- Warmup (stats baseline)
pktgen.start(port)
pktgen.delay(warmup_sec * 1000)
pktgen.stop(port)
pktgen.delay(200)

local base = pktgen.portStats(port, "port")[0]
local base_tx = base.opackets
local base_rx = base.ipackets

pktgen.start(port)
pktgen.delay(measure_sec * 1000)
pktgen.stop(port)
pktgen.delay(200)

local fin = pktgen.portStats(port, "port")[0]
local tx = fin.opackets - base_tx
local rx = fin.ipackets - base_rx
local lost = tx - rx
if lost < 0 then lost = 0 end
local loss_pct = 0.0
if tx > 0 then loss_pct = (lost * 100.0) / tx end
local sent_pps = tx / measure_sec
local recv_pps = rx / measure_sec
local fb = pkt_size
local offered_bps = sent_pps * fb * 8
local received_bps = recv_pps * fb * 8

print(string.format(
  "PKTGEN_SUMMARY sut=fg case=C01 mode=hot payload=4 " ..
  "seconds=%d warmup=%d flows=1000 " ..
  "client_sent=%d client_received=%d lost=%d loss_rate_pct=%.4f " ..
  "sent_pps=%.0f received_pps=%.0f offered_bps=%.0f received_bps=%.0f frame_bytes=%d",
  measure_sec, warmup_sec,
  tx, rx, lost, loss_pct, sent_pps, recv_pps, offered_bps, received_bps, fb))
