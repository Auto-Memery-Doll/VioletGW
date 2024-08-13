package consistent


type uints []uint32

func (x uints) Len() int {
	return len(x)	
}



type Consistent struct {
	circle 				map[uint32]string
	members				map[string]bool
	sortedHashes		uints
	NumberOfReplicas	int
	count				int64
	scratch 			[64]byte
	UseFnv				bool
	sync
}
