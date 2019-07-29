use crate::{prelude::*, protocols::ethernet2, test};
use float_duration::FloatDuration;
use serde_yaml;
use std::time::{Duration, Instant};

#[test]
fn immediate_reply() {
    // tests to ensure that an are request results in a reply.
    let now = Instant::now();
    let mut alice = test::new_alice(now);
    let mut bob = test::new_bob(now);
    let mut carrie = test::new_carrie(now);

    // this test is written based on certain assumptions.
    let options = alice.options();
    assert_eq!(
        options.arp.request_timeout.unwrap(),
        FloatDuration::seconds(1.0)
    );

    let fut = alice.arp_query(*test::carrie_ipv4_addr());
    let now = now + Duration::from_millis(1);
    assert!(fut.poll(now).is_none());

    let request = {
        let event = alice.poll(now).unwrap().unwrap();
        match event {
            Event::Transmit(datagram) => datagram.to_vec(),
            e => panic!("got unanticipated event `{:?}`", e),
        }
    };

    assert!(request.len() >= ethernet2::MIN_PAYLOAD_SIZE);

    // bob hasn't heard of alice before, so he will ignore the request.
    info!("passing ARP request to bob (should be ignored)...");
    match bob.receive(&request) {
        Err(Fail::Ignored {}) => (),
        x => panic!("expected Fail::Ignored {{}}, got `{:?}`", x),
    }
    let cache = bob.export_arp_cache();
    assert!(cache.get(test::alice_ipv4_addr()).is_none());

    carrie.receive(&request).unwrap();
    info!("passing ARP request to carrie...");
    let cache = carrie.export_arp_cache();
    assert_eq!(
        cache.get(test::alice_ipv4_addr()),
        Some(test::alice_link_addr())
    );
    let reply = {
        let event = carrie.poll(now).unwrap().unwrap();
        match event {
            Event::Transmit(datagram) => datagram.to_vec(),
            e => panic!("got unanticipated event `{:?}`", e),
        }
    };

    info!("passing ARP reply back to alice...");
    alice.receive(&reply).unwrap();
    debug!(
        "ARP cache contains: \n{}",
        serde_yaml::to_string(&alice.export_arp_cache()).unwrap()
    );
    let now = now + Duration::from_millis(1);
    let link_addr = fut.poll(now).unwrap().unwrap();
    assert_eq!(*test::carrie_link_addr(), link_addr);
}

#[test]
fn slow_reply() {
    // tests to ensure that an are request results in a reply.
    let now = Instant::now();
    let mut alice = test::new_alice(now);
    let mut bob = test::new_bob(now);
    let mut carrie = test::new_carrie(now);

    // this test is written based on certain assumptions.
    let options = alice.options();
    assert!(options.arp.retry_count.unwrap() > 0);
    assert_eq!(
        options.arp.request_timeout.unwrap(),
        FloatDuration::seconds(1.0)
    );

    let fut = alice.arp_query(*test::carrie_ipv4_addr());
    // move time forward enough to trigger a timeout.
    let now = now + Duration::from_secs(1);
    assert!(fut.poll(now).is_none());

    debug!("!!!");
    let request = {
        let event = alice.poll(now).unwrap().unwrap();
        match event {
            Event::Transmit(datagram) => datagram.to_vec(),
            e => panic!("got unanticipated event `{:?}`", e),
        }
    };

    debug!("???");
    assert!(request.len() >= ethernet2::MIN_PAYLOAD_SIZE);

    // bob hasn't heard of alice before, so he will ignore the request.
    info!("passing ARP request to bob (should be ignored)...");
    match bob.receive(&request) {
        Err(Fail::Ignored {}) => (),
        x => panic!("expected Fail::Ignored {{}}, got `{:?}`", x),
    }
    let cache = bob.export_arp_cache();
    assert!(cache.get(test::alice_ipv4_addr()).is_none());

    carrie.receive(&request).unwrap();
    info!("passing ARP request to carrie...");
    let cache = carrie.export_arp_cache();
    assert_eq!(
        cache.get(test::alice_ipv4_addr()),
        Some(test::alice_link_addr())
    );
    let reply = {
        let event = carrie.poll(now).unwrap().unwrap();
        match event {
            Event::Transmit(datagram) => datagram.to_vec(),
            e => panic!("got unanticipated event `{:?}`", e),
        }
    };

    info!("passing ARP reply back to alice...");
    alice.receive(&reply).unwrap();
    debug!(
        "ARP cache contains: \n{}",
        serde_yaml::to_string(&alice.export_arp_cache()).unwrap()
    );
    let now = now + Duration::from_millis(1);
    let link_addr = fut.poll(now).unwrap().unwrap();
    assert_eq!(*test::carrie_link_addr(), link_addr);
}

#[test]
fn no_reply() {
    // tests to ensure that an are request results in a reply.
    let now = Instant::now();
    let alice = test::new_alice(now);

    // this test is written based on certain assumptions.
    let options = alice.options();
    assert_eq!(options.arp.retry_count.unwrap(), 2);
    assert_eq!(
        options.arp.request_timeout.unwrap(),
        FloatDuration::seconds(1.0)
    );

    let fut = alice.arp_query(*test::carrie_ipv4_addr());
    // retry #1
    let now = now + Duration::from_secs(1);
    assert!(fut.poll(now).is_none());

    // retry #2
    let now = now + Duration::from_secs(1);
    assert!(fut.poll(now).is_none());

    // timeout
    let now = now + Duration::from_secs(1);
    match fut.poll(now).unwrap() {
        Err(Fail::Timeout {}) => (),
        x => panic!("expected Fail::Timeout {{}}, got `{:?}`", x),
    }
}
