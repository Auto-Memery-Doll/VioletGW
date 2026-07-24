// Command fgcp publishes upstream list + balance policy into flow_gateway SHM.
// Layout matches src/vg_cp_shm.h (little-endian, natural alignment).
package main

import (
	"encoding/binary"
	"flag"
	"fmt"
	"net"
	"os"
	"strconv"
	"strings"

	"golang.org/x/sys/unix"
)

const (
	magic      = 0x46474350 // 'FGCP'
	maxEP      = 64
	defaultSHM = "/flow_gateway_cp"
	policyMod  = 0
	policyRR   = 1
	shmBytes   = 4 + 4 + 1 + 1 + 2 + maxEP*8 // 524
)

type endpoint struct {
	ipBE uint32
	port uint16
}

type stringList []string

func (s *stringList) String() string { return strings.Join(*s, ",") }
func (s *stringList) Set(v string) error {
	*s = append(*s, v)
	return nil
}

func main() {
	shmName := flag.String("shm", defaultSHM, "POSIX SHM name")
	policyStr := flag.String("policy", "mod", "balance policy: mod|rr")
	var ups stringList
	flag.Var(&ups, "upstream", "upstream endpoint ip:port (repeatable)")
	flag.Parse()

	if len(ups) == 0 {
		fmt.Fprintf(os.Stderr, "usage: fgcp [-shm name] [-policy mod|rr] -upstream ip:port [...]\n")
		os.Exit(2)
	}

	policy := uint8(policyMod)
	switch strings.ToLower(*policyStr) {
	case "mod":
		policy = policyMod
	case "rr":
		policy = policyRR
	default:
		fmt.Fprintf(os.Stderr, "unknown policy %q\n", *policyStr)
		os.Exit(2)
	}

	eps, err := parseUpstreams(ups)
	if err != nil {
		fmt.Fprintf(os.Stderr, "%v\n", err)
		os.Exit(2)
	}
	if len(eps) > maxEP {
		fmt.Fprintf(os.Stderr, "too many upstreams (max %d)\n", maxEP)
		os.Exit(2)
	}

	if err := publish(*shmName, policy, eps); err != nil {
		fmt.Fprintf(os.Stderr, "publish failed: %v\n", err)
		os.Exit(1)
	}
	fmt.Printf("published shm=%s policy=%s upstreams=%d\n", *shmName, *policyStr, len(eps))
}

func parseUpstreams(ss []string) ([]endpoint, error) {
	eps := make([]endpoint, 0, len(ss))
	for _, s := range ss {
		host, portStr, err := net.SplitHostPort(s)
		if err != nil {
			return nil, fmt.Errorf("bad upstream %q: %w", s, err)
		}
		ip := net.ParseIP(host)
		if ip == nil {
			return nil, fmt.Errorf("bad ip in %q", s)
		}
		ip4 := ip.To4()
		if ip4 == nil {
			return nil, fmt.Errorf("need IPv4 in %q", s)
		}
		port, err := strconv.ParseUint(portStr, 10, 16)
		if err != nil {
			return nil, fmt.Errorf("bad port in %q", s)
		}
		eps = append(eps, endpoint{
			ipBE: binary.BigEndian.Uint32(ip4),
			port: uint16(port),
		})
	}
	return eps, nil
}

func publish(name string, policy uint8, eps []endpoint) error {
	// Linux: POSIX SHM objects live under /dev/shm.
	path := name
	if strings.HasPrefix(name, "/") {
		path = "/dev/shm" + name
	} else {
		path = "/dev/shm/" + name
	}

	fd, err := unix.Open(path, unix.O_RDWR|unix.O_CREAT, 0660)
	if err != nil {
		return fmt.Errorf("open %s: %w", path, err)
	}
	defer unix.Close(fd)

	if err := unix.Ftruncate(fd, shmBytes); err != nil {
		return fmt.Errorf("ftruncate: %w", err)
	}

	b, err := unix.Mmap(fd, 0, shmBytes, unix.PROT_READ|unix.PROT_WRITE, unix.MAP_SHARED)
	if err != nil {
		return fmt.Errorf("mmap: %w", err)
	}
	defer unix.Munmap(b)

	cur := binary.LittleEndian.Uint32(b[4:8])
	next := cur + 1
	if next == 0 {
		next = 1
	}

	binary.LittleEndian.PutUint32(b[0:4], magic)
	b[8] = policy
	b[9] = 0
	binary.LittleEndian.PutUint16(b[10:12], uint16(len(eps)))
	off := 12
	for i := 0; i < maxEP; i++ {
		if i < len(eps) {
			binary.LittleEndian.PutUint32(b[off:off+4], eps[i].ipBE)
			binary.LittleEndian.PutUint16(b[off+4:off+6], eps[i].port)
			binary.LittleEndian.PutUint16(b[off+6:off+8], 0)
		} else {
			for j := 0; j < 8; j++ {
				b[off+j] = 0
			}
		}
		off += 8
	}

	binary.LittleEndian.PutUint32(b[4:8], next)
	return unix.Msync(b, unix.MS_SYNC)
}
