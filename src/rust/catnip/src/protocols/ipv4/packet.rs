use super::header::{
    checksum::Hasher, Ipv4Header, Ipv4HeaderMut, DEFAULT_IPV4_TTL,
    IPV4_HEADER_SIZE, IPV4_IHL_NO_OPTIONS, IPV4_VERSION,
};
use crate::{prelude::*, protocols::ethernet2};
use std::convert::TryFrom;

pub struct Ipv4Packet<'a>(ethernet2::Frame<'a>);

impl<'a> Ipv4Packet<'a> {
    pub fn header(&self) -> Ipv4Header<'_> {
        Ipv4Header::new(&self.0.payload()[..IPV4_HEADER_SIZE])
    }

    pub fn frame(&self) -> &ethernet2::Frame<'a> {
        &self.0
    }

    pub fn payload(&self) -> &[u8] {
        &self.0.payload()[IPV4_HEADER_SIZE..]
    }
}

impl<'a> TryFrom<ethernet2::Frame<'a>> for Ipv4Packet<'a> {
    type Error = Fail;

    fn try_from(frame: ethernet2::Frame<'a>) -> Result<Self> {
        trace!("Ipv4Packet::try_from(...)");
        assert_eq!(frame.header().ether_type()?, ethernet2::EtherType::Ipv4);
        if frame.payload().len() <= IPV4_HEADER_SIZE {
            return Err(Fail::Malformed {});
        }

        let packet = Ipv4Packet(frame);
        let payload_len = packet.payload().len();
        let header = packet.header();
        if header.version() != IPV4_VERSION {
            return Err(Fail::Unsupported {});
        }

        if header.total_len() != payload_len + IPV4_HEADER_SIZE {
            return Err(Fail::Malformed {});
        }

        let ihl = header.ihl();
        if ihl < IPV4_IHL_NO_OPTIONS {
            return Err(Fail::Malformed {});
        }

        // we don't currently support IPv4 options.
        if ihl > IPV4_IHL_NO_OPTIONS {
            return Err(Fail::Unsupported {});
        }

        // we don't currently support fragmented packets.
        if header.frag_offset() != 0 {
            return Err(Fail::Unsupported {});
        }

        // from _TCP/IP Illustrated_, Section 5.2.2:
        // > Note that for any nontrivial packet or header, the value
        // > of the Checksum field in the packet can never be FFFF.
        let checksum = header.checksum();
        if checksum == 0xffff {
            return Err(Fail::Malformed {});
        }

        let should_be_zero = {
            let mut hasher = Hasher::new();
            hasher.write(header.as_bytes());
            hasher.finish()
        };

        if checksum != 0 && should_be_zero != 0 {
            return Err(Fail::Malformed {});
        }

        let _ = header.protocol()?;
        Ok(packet)
    }
}

pub struct Ipv4PacketMut<'a>(ethernet2::FrameMut<'a>);

impl<'a> Ipv4PacketMut<'a> {
    pub fn from_bytes(bytes: &'a mut [u8]) -> Result<Self> {
        Ok(Ipv4PacketMut(ethernet2::FrameMut::from_bytes(bytes)?))
    }

    pub fn header(&mut self) -> Ipv4HeaderMut<'_> {
        Ipv4HeaderMut::new(&mut self.0.payload()[..IPV4_HEADER_SIZE])
    }

    pub fn frame(&mut self) -> &mut ethernet2::FrameMut<'a> {
        &mut self.0
    }

    pub fn payload(&mut self) -> &mut [u8] {
        &mut self.0.payload()[IPV4_HEADER_SIZE..]
    }

    pub fn unmut(self) -> Ipv4Packet<'a> {
        Ipv4Packet(self.0.unmut())
    }

    pub fn seal(mut self) -> Result<Ipv4Packet<'a>> {
        let payload_len = self.payload().len();
        let total_len = IPV4_HEADER_SIZE + payload_len;

        {
            let mut ipv4_header = self.header();
            ipv4_header.version(IPV4_VERSION);
            ipv4_header.ihl(IPV4_IHL_NO_OPTIONS);
            ipv4_header.ttl(DEFAULT_IPV4_TTL);
            ipv4_header.total_len(u16::try_from(total_len)?);

            let mut hasher = Hasher::new();
            hasher.write(&ipv4_header.as_bytes()[..10]);
            hasher.write(&ipv4_header.as_bytes()[12..]);
            ipv4_header.checksum(hasher.finish());
        }

        let mut frame_header = self.frame().header();
        frame_header.ether_type(ethernet2::EtherType::Ipv4);
        Ok(self.unmut())
    }
}

impl<'a> From<ethernet2::FrameMut<'a>> for Ipv4PacketMut<'a> {
    fn from(frame: ethernet2::FrameMut<'a>) -> Self {
        Ipv4PacketMut(frame)
    }
}
