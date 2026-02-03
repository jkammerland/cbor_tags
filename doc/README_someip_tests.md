# SOME/IP tests (UDS E2E) — how it works

This document explains the Unix-domain-socket (UDS) end‑to‑end test that exercises:

- Service discovery (Find/Offer, Subscribe/Ack)
- Field setter (writable vs read‑only)
- Event notifications (10 events)
- Basic method request/response

The test is implemented in `test/test_someip_uds_e2e.cpp`.

## Overview

Two processes are used:

- **Parent (client)**: drives the E2E scenario.
- **Child (server)**: implements SD, field setter, notifications, and a method.

The parent and child communicate over a single AF_UNIX/SOCK_STREAM socket.

## Pseudocode

### Child (server)

```
start_server(socket_path):
  create AF_UNIX socket
  bind + listen
  signal parent "ready"
  accept client

  subscribed = false
  field_value = 0

  loop:
    frame = recv_someip_frame()
    parsed = parse_someip(frame)

    if frame is SD message:
      if entry == FindService:
        send OfferService
      if entry == SubscribeEventgroup:
        send SubscribeAck
        subscribed = true
      continue

    if message == Shutdown (REQUEST_NO_RETURN):
      break

    if message == Field Setter (writable):
      decode new value
      field_value = new value
      send RESPONSE (E_OK)
      if subscribed:
        for i in 0..9:
          send NOTIFICATION (seq=i, value=field_value)
      continue

    if message == Field Setter (read‑only):
      send ERROR (E_NOT_OK)
      continue

    if message == Method Request:
      decode request
      send RESPONSE (E_OK) with payload
      continue

    else:
      fail test (unexpected message)

  close socket and exit
```

### Parent (client)

```
run_test():
  fork child
  wait for "ready" byte
  connect to socket

  send SD FindService
  receive OfferService

  send SD SubscribeEventgroup
  receive SubscribeAck

  send Field Setter (writable)
  receive RESPONSE (E_OK)

  for i in 0..9:
    receive NOTIFICATION
    assert seq == i
    assert value == 0xBEEF

  send Field Setter (read‑only)
  receive ERROR (E_NOT_OK)

  send Method Request
  receive RESPONSE (E_OK)

  send Shutdown (REQUEST_NO_RETURN)
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

