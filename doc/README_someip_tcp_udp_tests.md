# SOME/IP TCP/UDP E2E tests — how it works

This document explains the TCP/UDP end‑to‑end tests that mirror the UDS/SHM flow
using IP sockets. The tests live in `test/test_someip_tcp_udp_e2e.cpp` and cover:

- Service discovery (Find/Offer, Subscribe/Ack over UDP)
- Field setter (writable vs read‑only)
- Event notifications (10 events)
- Method request/response

Two transports are exercised:

- **TCP (reliable)**: service traffic over TCP, SD over UDP
- **UDP (unreliable)**: service traffic over UDP, SD over UDP

## Overview

Two processes are used (server + parent), with two client endpoints:

- **Parent (client)**: drives the E2E scenario and opens **two** client endpoints.
- **Child (server)**: implements SD, field setter, notifications, and a method.

The server binds ephemeral ports and sends them to the parent over a pipe so
parallel test runs do not collide on fixed ports.

## Pseudocode

### Child (server)

```
start_server(reliable):
  sd_sock = bind_udp(port=0)                 // SD socket
  if reliable:
    listen_sock = bind_tcp(port=0)           // service socket
  else:
    service_sock = bind_udp(port=0)
  send parent {sd_port, service_port}

  subscribed_tcp = {}
  subscribed_udp = {}
  field_value = 0

  loop:
    poll sd_sock (+ listen_sock/service_sock + client sockets)

    if SD datagram:
      decode SD
      if FindService:
        send OfferService (with endpoint=service_port)
      if SubscribeEventgroup:
        send SubscribeAck
        record subscriber endpoint (TCP or UDP)

    if reliable and listen_sock readable:
      accept client, record peer addr

    if service message:
      if Shutdown:
        break
      if Field Setter (writable):
        decode value; field_value = value
        reply RESPONSE E_OK
        send 10 NOTIFICATION events to all subscribed clients
      if Field Setter (read‑only):
        reply ERROR E_NOT_OK
      if Method Request:
        reply RESPONSE (payload)
```

### Parent (client)

```
run_test(reliable):
  fork child server
  read sd_port + service_port

  sd_sock = bind_udp(port=0)

  send FindService → recv OfferService (extract service_port)

  create client A + client B endpoints:
    TCP: connect to service_port, record local ports
    UDP: bind two UDP sockets, record local ports

  send SubscribeEventgroup for A + B (endpoint = local port)
  receive SubscribeAck for A + B

  client A: Field Setter (writable) → RESPONSE E_OK
  client A: receive 10 NOTIFICATION events
  client B: receive 10 NOTIFICATION events

  client B: Field Setter (read‑only) → ERROR E_NOT_OK

  client A: Method Request → RESPONSE (payload)
  client A: Shutdown (REQUEST_NO_RETURN)
  wait for child exit
```

## Notes

- SD messages are exchanged over UDP even when the service uses TCP.
- Ports are ephemeral and passed from child → parent over a pipe.
- The tests follow the same field/event/method semantics as the UDS/SHM tests.
