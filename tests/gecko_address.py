"""Aroma Gecko uses 32-bit signed strtol, then casts to uint32_t."""
def address_argument(address):
    if not isinstance(address,int) or not 0<=address<=0xffffffff:
        raise ValueError('Address must be uint32')
    return str(address if address<0x80000000 else address-0x100000000)

if __name__=='__main__':
    for address in (0,0x104741e4,0x7fffffff,0x80000000,0x80702b38,0xffffffff):
        encoded=address_argument(address)
        value=int(encoded,0)
        assert -0x80000000<=value<=0x7fffffff
        assert value&0xffffffff==address
    print('PASS: Gecko signed address encoding, including plugin high memory')
