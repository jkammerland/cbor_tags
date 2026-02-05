# SOME/IP tests (UDS E2E) — how it works

This document explains the Unix-domain-socket (UDS) end‑to‑end test that exercises:

- Service discovery (Find/Offer, Subscribe/Ack)
- Field setter (writable vs read‑only)
- Event notifications (10 events)
- Basic method request/response

The test is implemented in `test/test_someip_uds_e2e.cpp`.

## Overview

Two processes are used (server + parent), with two client connections:

- **Parent (client)**: drives the E2E scenario and opens **two** client connections.
- **Child (server)**: implements SD, field setter, notifications, and a method.

The parent and child communicate over a single AF_UNIX/SOCK_STREAM socket.

## Pseudocode

### Child (server)

```
start_server(socket_path):
  create AF_UNIX socket
  bind + listen
  signal parent "ready"
  accept client A
  accept client B

  subscribed[A] = false
  subscribed[B] = false
  field_value = 0

  loop:
    for each readable client:
      frame = recv_someip_frame(client)
      parsed = parse_someip(frame)

      if frame is SD message:
        if entry == FindService:
          send OfferService to that client
        if entry == SubscribeEventgroup:
          send SubscribeAck to that client
          subscribed[client] = true
        continue

      if message == Shutdown (REQUEST_NO_RETURN):
        break

      if message == Field Setter (writable):
        decode new value
        field_value = new value
        send RESPONSE (E_OK) to caller
        for each subscribed client:
          for i in 0..9:
            send NOTIFICATION (seq=i, value=field_value)
        continue

      if message == Field Setter (read‑only):
        send ERROR (E_NOT_OK) to caller
        continue

      if message == Method Request:
        decode request
        send RESPONSE (E_OK) with payload
        continue

      else:
        fail test (unexpected message)

  close sockets and exit
```

### Parent (client)

```
run_test():
  fork child
  wait for "ready" byte
  connect client A
  connect client B

  client A: FindService → OfferService
  client B: FindService → OfferService

  client A: SubscribeEventgroup → SubscribeAck
  client B: SubscribeEventgroup → SubscribeAck

  client A: Field Setter (writable)
  client A: RESPONSE (E_OK)

  client A: receive 10 NOTIFICATION events
  client B: receive 10 NOTIFICATION events

  client B: Field Setter (read‑only)
  client B: ERROR (E_NOT_OK)

  client A: Method Request → RESPONSE (E_OK)

  client A: Shutdown (REQUEST_NO_RETURN)
  wait for child exit
```

## Field IDs used in the test

```
writable_field:
  service_id        = 0x1234
  getter_method_id  = 0x0100
  setter_method_id  = 0x0101
  notifier_event_id = 0x8001
  eventgroup_id     = 0x0001

readonly_field:
  service_id        = 0x1234
  getter_method_id  = 0x0200
  setter_method_id  = 0x0201
  notifier_event_id = 0x8002
  eventgroup_id     = 0x0002
```

## Environment notes

- The test uses AF_UNIX sockets + `fork()`. On Windows, it’s skipped.
- If the environment forbids `bind()` for AF_UNIX sockets, the test exits early with a note.

## Shared memory variant

There is a Linux‑only shared‑memory E2E test in `test/test_someip_shm_e2e.cpp` that mirrors the same
multi‑client flow but uses `shm_open` + `mmap` and futex‑based queues instead of sockets. It covers:

- Find/Offer and Subscribe/Ack per client
- Writable setter → RESPONSE E_OK
- 10 notifier events to all subscribed clients
- Read‑only setter → ERROR E_NOT_OK
- Method request/response and shutdown

## TCP/UDP variant

There are TCP/UDP E2E tests in `test/test_someip_tcp_udp_e2e.cpp` that mirror the same multi‑client
flow over IP sockets. A dedicated walkthrough (with pseudocode) is in
`doc/README_someip_tcp_udp_tests.md`.

## vSomeIP TCP/UDP E2E (optional)

There is an optional vSomeIP‑backed E2E test in `test/vsomeip/vsomeip_e2e.cpp` that exercises the
same **field/event/method** flow using vSomeIP over **TCP (reliable)** and **UDP (unreliable)**.
It uses vSomeIP configuration files in `test/vsomeip/conf/` and runs three processes:

- **Server**: vSomeIP routing manager + service provider
- **Client A**: performs writable setter, receives events, calls method, then sends shutdown
- **Client B**: subscribes, receives events, verifies read‑only setter error

### Pseudocode (vSomeIP)

```
main():
  run_transport(tcp_config, reliable=true)
  run_transport(udp_config, reliable=false)

run_transport(config):
  set VSOMEIP_CONFIGURATION=config
  fork server -> wait for "ready"
  fork client_b (subscriber)
  fork client_a (driver)
  wait client_b done
  signal client_a to shutdown
  wait all children

server:
  offer service + field event
  on writable setter:
    reply E_OK
    send 10 notifications (seq 0..9)
  on read‑only setter:
    reply ERROR E_NOT_OK
  on method:
    reply with payload
  on shutdown:
    stop

client_a:
  request service, subscribe
  send writable setter, expect E_OK
  receive 10 notifications
  send method request, expect response
  wait "go" signal, send shutdown

client_b:
  request service, subscribe
  receive 10 notifications
  send read‑only setter, expect ERROR E_NOT_OK
  signal "done"
```
