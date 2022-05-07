# MoarVM Remote Debug Protocol Design

The MoarVM Remote Debug Protocol is used to control a MoarVM instance over a
socket, for the purposes of debugging. The VM must have been started in debug
mode for this capability to be available. This document defines the debug
protocol's wire format.

## The wire format

Rather than invent Yet Another Custom Binary Protocol, the MoarVM remote debug
protocol uses [MessagePack](https://msgpack.org/). This has the advantage of
easy future extensibility and existing support from other languages.

The only thing that is not MessagePack is the initial handshake, leaving the
freedom to move away from MessagePack in a future version, should there ever
be cause to do so.

Since MessagePack is largely just a more compact way to specify JSON, all of
the messages in this document are demonstrated with JSON syntax. This is just
for ease of reading; JSON is not used at all on the wire.

### Initial Handshake

Upon receving a connection, MoarVM will immediately send the following 24
bytes if it is willing and able to accept the connection:

* The string "MOARVM-REMOTE-DEBUG\0" encoded in ASCII
* A big endian, unsigned, 16-bit major protocol version number
* A big endian, unsigned, 16-bit minor protocol version number

Otherwise, it will send the following response, explaining why it cannot,
and then close the connection:

* The string "MOARVM-REMOTE-DEBUG!" encoded in ASCII
* A big endian, unsigned, 16-bit length for an error string explaining the
  rejection (length in bytes)
* The error string, encoded in UTF-8

A client that receives anything other than a response of this form must close
the connection and report an error. A client that receives an error response
must report the error.

Otherwise, the client should check if it is able to support the version of the
protocol that the server speaks. The onus is on clients to support multiple
versions of the protocol should the need arise. See versioning below for more.
If the client does not wish to proceed, it should simply close the connection.

If the client is statisfied with the version, it should send:

* The string "MOARVM-REMOTE-CLIENT-OK\0" encoded in ASCII

For the versions of the protocol defined in this document, all further
communication  will be in terms of MessagePack messages.

## MessagePack envelope

Every exchange using MessagePack must be an object at the top level. The
object must always have the following keys:

* `type`, which must have an integer value. This specifies the type of the
  message. Failing to include this field or failing to have its value be
  an integer is a protocol error, and any side receiving such a message
  should terminate the connection.
* `id`, which must have an integer value. This is used to associate a
  response with a request, where required. Any interaction initiated by the
  client should have an odd `id`, starting from 1. Any interaction initiated
  by the server should have an even `id`, starting from 2.

The object may contain further keys, which will be determined by message type.

## Versioning

Backwards-incompatible changes, if needed, will be made by incrementing the
major version number. A client seeing a major version number it does not
recognize or support must close the connection and not attempt any further
interaction, and report an error.

The minor version number is incremented for backwards-compatible changes. A
client may proceed safely with a higher minor version number of the protocol
than it knows about. However, it should be prepared to accept and disregard
message types that it does not recognize, as well as any keys in an object
(encoded using MessagePack) that it does not recognize.

The client can use the minor version number to understand what features are
supported by the remote MoarVM instance.

The MoarVM instance must disregard keys in a MessagePack object that it does
not understand. For message types that it does not recognize, it must send a
message of type "Message Type Not Understood" (format defined below); the
connection should be left intact by MoarVM, and the client can decide how to
proceed.

### Changes

#### Version 1.3 ERRATA

 * **No behavior changes**, only the following errata fixes in this doc.
 * Fix name of message type to send as response if request message type is
   not recognized in Versioning text above.
 * Remove note that Error Processing Message (1) should not be sent if a
   request is missing required keys; that's exactly what is sent in that case.
 * Clarify that suspension of "all" threads actually refers to all *user*
   threads (threads other than the spesh worker and the debug server itself).
 * Note which specific error is sent if a thread ID is not recognized.
 * Remove note that Step Out (22) requires a handle to step out of (it does
   not; it requires exactly the same keys as the other Step requests).
 * Fix a copy/paste typo in the description of Caller Context Request (30).
 * Note that Find Method (35) has been unsupported (and returns an error if
   requested) since `new-disp` landed.
 * Note that Operation Unsuccessful (39) is not in current use, as Error
   Processing Message (1) is used instead.
 * Clarify in example that Object Positionals Request (42) requires a handle
   of the object to be examined.
 * Fill in description of Object Associatives Response (45).
 * Correct names for messages types that did not match their enumerant names.
 * Correct several minor typos.

#### Version 1.3

 * Change second "type" key in Invoke Result (37) to "obj_type".
 * Add HLL Symbol Request (48) and HLL Symbol Response (49) message types.

#### Version 1.2

Added a "name" field to threads in Thread List Response (12)

## Security considerations

Any client connected to the debug protocol will be able to perform remote code
execution using the running MoarVM instance. Therefore, MoarVM must only bind
to `localhost` by default. It may expose an option to bind to further
interfaces, but should display a warning about the dangers of this option.

Remote debugging should be performed by establishing a secure tunnel from the
client to the server, for example using SSH port forwarding. This provides
both authentication and protection against tampering with messages.

## Message types

All messages defined here, unless stated otherwise, are supported in major
version 1, minor version 0, of the protocol (also known as `1.0`).

### Message Type Not Understood (0)

Sent only by MoarVM to indicate that a message type was not understood. The
ID correlates it with the message that was not understood.

    {
        "type": 0,
        "id": $id
    }

### Error Processing Message (1)

Sent only by MoarVM to indicate that a problem occurred with processing a
message. The ID correlates it with the message that was not understood. The
`reason` key should be a string explaining why.

    {
        "type": 1,
        "id": $id,
        "reason": "Program already terminated"
    }

### Operation Successful (2)

This is a generic message sent by the MoarVM instance to acknowledge that an
operation was successfully performed. This is sent in response to commands,
when there is no further information to return. The ID correlates it with the
message from the client that was successfully processed.

    {
        "type": 2,
        "id": $id
    }

### Is Execution Suspended Request (3)

Sent by the client to ask the MoarVM instance if execution of all user threads
(threads other than the spesh worker and the debug server itself) is currently
suspended.

    {
        "type": 3,
        "id": $id
    }

### Is Execution Suspended Response (4)

Response from the MoarVM instance, with the `suspended` key set to `true` if
execution is currently suspended on all user threads (threads other than the
spesh worker and the debug server itself) and `false` otherwise.

    {
        "type": 4,
        "id": $id,
        "suspended": true
    }

### Suspend All (5)

Requests that all user threads (threads other than the spesh worker and the
debug server itself) be suspended. Once this has happened, an Operation
Successful message will be sent. If all threads were already suspended, then
nothing happens and an Operation Successful message will be sent.

    {
        "type": 5,
        "id": $id
    }

### Resume All (6)

Requests that all suspended threads be resumed. Once this has happened, an
Operation Successful message will be sent. If no threads were suspended, then
nothing happens and an Operation Successful message will be sent.

    {
        "type": 6,
        "id": $id
    }

### Suspend One (7)

Requests that a specific thread be suspended, with the thread ID specified by
the `thread` key. Once this has happened, an Operation Successful message will
be sent. If the threads was already suspended, then nothing happens and an
Operation Successful message will also be sent. An Error Processing Message
error will be reported if the thread ID is not recognized.

    {
        "type": 7,
        "id": $id,
        "thread": 1
    }

### Resume One (8)

Requests that a specific thread be resumed, with the thread ID specified by the
`thread` key. Once this has happened, an Operation Successful message will be
sent. If the thread was found but not suspended, then nothing happens and an
Operation Successful message will be sent.  If the thread ID was not
recognized, an Error Processing Message error will be sent.

    {
        "type": 8,
        "id": $id,
        "thread": 1
    }

### Thread Started (9)

This message is sent by MoarVM whenever a new thread is started. The client
can simply disregard it if it has no interest in this information.

    {
        "type": 9,
        "id": $id,
        "thread": 3,
        "native_id": 1020,
        "app_lifetime": true
    }

### Thread Ended (10)

This message is sent by MoarVM whenever a thread terminates. The client can
simply disregard it if it has no interest in this information.

    {
        "type": 10,
        "id": $id,
        "thread": 3
    }

### Thread List Request (11)

This message is sent by the client to request a list of all threads, with
some information about each one. This may be sent at any time, whether or
not the threads are suspended.

    {
        "type": 11,
        "id": $id
    }

### Thread List Response (12)

This message is sent as a response to a Thread List Request. It contains an
array of objects, with one entry per running threads, providing information
about that thread. It also contains an indication of whether the threads was
suspended, and the number of locks it is currently holding.

The name field was added in version 1.2

    {
        "type": 12,
        "id": $id,
        "threads": [
            {
                "thread": 1,
                "native_id": 1010,
                "app_lifetime": false,
                "suspended": true,
                "num_locks": 1,
                "name": "AffinityWorker",
            },
            {
                "thread": 3,
                "native_id": 1020,
                "app_lifetime": true,
                "suspended": false,
                "num_locks": 0,
                "name": "Supervisor",
            }
        ]
    }

### Thread Stack Trace Request (13)

This message is sent by the client to request the stack trace of a thread.
This is only allowed if that thread is suspended; an error will be returned
otherwise.

    {
        "type": 13,
        "id": $id,
        "thread": 3
    }

### Thread Stack Trace Response (14)

This message is sent by MoarVM in response to a Thread Stack Trace Request. It
contains an array of stack frames, topmost first, that are currently on the
call stack of that thread. Each stack frame is represented by an object. The
`bytecode_file` key will be either a string or `nil` if the bytecode only exists
"in memory" (for example, due to an `EVAL`). The `name` key will be an empty
string in the case that the code for that frame has no name. The `type` field
is the debug name of the type of the code object, or `nil` if there is none.

    {
        "type": 14,
        "id": $id,
        "frames": [
            {
                "file": "path/to/source/file",
                "line": 22,
                "bytecode_file": "path/to/bytecode/file",
                "name": "some-method",
                "type": "Method"
            },
            {
                "file": "path/to/source/file",
                "line": 12,
                "bytecode_file": "path/to/bytecode/file",
                "name": "",
                "type": "Block"
            },
            {
                "file": "path/to/another/source/file",
                "line": 123,
                "bytecode_file": "path/to/another/bytecode/file",
                "name": "foo",
                "type": nil
            }
        ]
    }

### Set Breakpoint Request (15)

Sent by the client to set a breakpoint at the specified location, or the
closest possible location to it. The file refers to the source file. If
`suspend` is set to `true` then execution of all threads will be suspended
when the breakpoint is hit. In either case, the client will be notified. The
use of non-suspend breakpoints is for simply counting the number of times a
certain point is crossed. If the `stacktrace` option is set to `true` then a
stack trace of the location where the breakpoint was hit will be included.
This can be used both with and without `suspend`; with `suspend` it can save
an extra round-trip to reqeust the stack location, while without `suspend` it
can be useful for features like "capture a stack trace every time foo is
called".

    {
        "type": 15,
        "id": $id,
        "file": "path/to/source/file",
        "line": 123,
        "suspend": true,
        "stacktrace": false
    }

### Set Breakpoint Confirmation (16)

Sent by MoarVM to confirm that a breakpoint has been set. The `line` key
indicates the actual line that the breakpoint was placed on, if there was no
exactly annotation match. This message must be sent before any breakpoint
notifications; the ID will match the breakpoint request.

    {
        "type": 16,
        "id": $id,
        "line": 123
    }

### Breakpoint Notification (17)

Sent by MoarVM whenever a breakpoint is hit. The ID will match that of the
breakpoint request. The `frames` key will be `nil` if the `stacktrace` key of
the breakpoint request was `false`. Otherwise, it will contain an array of
objects describing the stack frames, formatted as in the Thread Stack Trace
Response message type.

    {
        "type": 17,
        "id": $id,
        "thread": 1,
        "frames": nil
    }

### Clear Breakpoint (18)

Clears a breakpoint. The line number must be the one the breakpoint was really
set on (indicated in the Set Breakpoint Confirmation message). After clearing
the breakpoint, MoarVM will send an Operation Successful response.

    {
        "type": 18,
        "id": $id,
        "file": "path/to/source/file",
        "line": 123
    }

### Clear All Breakpoints (19)

Clears all breakpoints that have been set. Once they have been cleared, MoarVM
will respond with an Operation Successful message.

    {
        "type": 19,
        "id": $id
    }

### Single Step (aka. Step Into) (20)

Runs until the next program point, where program points are determined by
either a change of frame or a change of line number in the bytecode annotation
table. The thread this is invoked on must be suspended, and will be returned
to suspended state after the step has taken place. A Step Completed message
will be sent by MoarVM at that point.

    {
        "type": 20,
        "id": $id,
        "thread": 1
    }

### Step Over (21)

Runs until the next program point either in the same frame or in a calling
frame, but not in any called frames below this point. The thread this is
invoked on must be suspended, and will be returned to suspended state after
the step has taken place. A Step Completed message will be sent by MoarVM at
that point.

    {
        "type": 21,
        "id": $id,
        "thread": 1
    }

### Step Out (22)

Runs until the program returns into the specified frame. The thread this is
invoked on must be suspended, and will be returned to suspended state after
the step has taken place. A Step Completed message will be sent by MoarVM at
that point.

    {
        "type": 22,
        "id": $id,
        "thread": 1
    }

### Step Completed (23)

Sent by MoarVM to acknowledge that a stepping operation was completed. The ID
matches that of the step request. The `frames` array contains the stacktrace
after stepping; the `file` and `line` of the current location being in the
topmost frame.

    {
        "type": 23,
        "id": $id,
        "thread": 1,
        "frames": [
            ...
        ]
    }

### Release Handles (24)

Handles are integers that are mapped to an object living inside of the VM.
For so long as the handle is alive, the object will be kept alive by being in
the handles mapping table. Therefore, it is important that, when using any
instructions that involve handles, they are released afterwards. Otherwise,
the debug client can induce a managed memory leak. This command is confirmed
with an Operation Successful message.

    {
        "type": 24,
        "id": $id,
        "handles": [42, 100]
    }

### Handle Result (25)

This is a common response message send by MoarVM for requests that ask for an
object handle. The ID will match that of the request. Remember to release
handles when the debug client no longer needs them by sending a Release Handles
message. The `0` handle represents the VM Null value.

    {
        "type": 25,
        "id": $id,
        "handle": 42
    }

### Context Handle (26)

Sent by the client to allocate a context object handle for the specified frame
(indicated by the depth relative to the topmost frame on the callstack, which
is frame 0) and thread. This can only be used on a thread that is suspended. A
context handle is just an object handle, where the object happens to have the
MVMContext REPR, and the result is delivered as a Handle Result message.

    {
        "type": 26,
        "id": $id,
        "thread": 1,
        "frame": 0
    }

### Context Lexicals Request (27)

Sent by the client to request the values of lexicals in a given context. The
`handle` key must be a context handle. The response comes as a Context
Lexicals Response message.

    {
        "type": 27,
        "id": $id,
        "handle": 1234
    }

### Context Lexicals Response (28)

Contains the results of introspecting a context. For natively typed values,
the value is included directly in the response. For object lexicals, an
object handle will be allocated for each one. This will allow for further
introspection of the object; take care to release it. The debug name of the
type is directly included, along with whether it's concrete (as opposed to a
type object) and a container type that could be decontainerized. The `kind`
key may be one of `obj`, `int`, `num`, or `str`.

    {
        "type": 28,
        "id": $id,
        "lexicals": {
            "$x": {
                "kind": "obj",
                "handle": 1234,
                "type": "Scalar",
                "concrete": true,
                "container": true
            },
            "$i": {
                "kind": "int",
                "value": 42
            },
            "$s": {
                "kind": "str",
                "value": "Bibimbap"
            }
        }
    }

### Outer Context Request (29)

Used by the client to gets a handle to the outer context of the one passed.
A Handle Result message will be sent in response. The null handle (0) will be
sent if there is no outer.

    {
        "type": 29,
        "id": $id,
        "handle": 1234
    }

### Caller Context Request (30)

Used by the client to gets a handle to the caller context of the one passed.
A Handle Result message will be sent in response. The null handle (0) will be
returned if there is no caller.

    {
        "type": 30,
        "id": $id,
        "handle": 1234
    }

### Code Object Handle (31)

Sent by the client to allocate a handle for the code object of the specified
frame (indicated by the depth relative to the topmost frame on the callstack,
which is frame 0) and thread. This can only be used on a thread that is
suspended. If there is no high-level code object associated with the frame,
then the null handle (0) will be returned. The response is delivered as a
Handle Result message.

    {
        "type": 31,
        "id": $id,
        "thread": 1,
        "frame": 0
    }

### Object Attributes Request (32)

Used by the client to introspect the attributes of an object. The response
comes as an Object Attributes Response message.

    {
        "type": 32,
        "id": $id,
        "handle": 1234
    }

### Object Attributes Response (33)

Contains the results of introspecting the attributes of an object. If the
object cannot have any attributes, the `attributes` key will be an empty
array. For natively typed attributes, the value is included directly in the
response. For object attributes, an object handle will be allocated for each
one. This will allow for further introspection of the object; take care to
release it. The debug name of the type is directly included, along with
whether it's concrete (as opposed to a type object) and a container type that
could be decontainerized. The `kind` key may be one of `obj`, `int`, `num`, or
`str`. Since attributes with the same name may exist at multiple inheritance
levels, an array is returned with the debug name of the type at that level
under the `class` key.

    {
        "type": 33,
        "id": $id,
        "attributes": [
            {
                "name": "$!x",
                "class": "FooBase"
                "kind": "obj",
                "handle": 1234,
                "type": "Scalar",
                "concrete": true,
                "container": true
            },
            {
                "name": "$!i",
                "class": "Foo",
                "kind": "int",
                "value": 42
            }
        ]
    }

### Decontainerize Handle (34)

Used to decontainerize a value in a container (such as a Raku `Scalar`). The
handle to the object that results is returned in a Handle Result message. If
this is not a container type, or if an exception occurs when trying to do the
decontainerization, an Error Processing Message response will be sent by MoarVM
instead. A target thread to perform this operation on is required, since it
may be required to run code (such as a `Proxy`); the thread must be suspended
at the point this request is issued, and will be returned to suspended state
again after the decontainerization has taken place. Note that breakpoints may
be hit and will be fired during this operation.

    {
        "type": 34,
        "id": $id,
        "thread": 1,
        "handle": 1234
    }

### Find Method (35)

**NOTE**: This request is no longer supported by newer MoarVM releases since
the conversion to the new dispatch system (`new-disp`); current releases will
send Error Processing Message instead of the following behavior.

Used by the client to find a method on an object that it has a handle to. The
handle to the method that results is returned in a Handle Result message, with
the null object handle (0) indicating no method found. If an exception occurs
when trying to do the method resolution, an Error Processing Message response
will be sent by MoarVM instead. A target thread to perform this operation on
is required, since it may be required to run code (such as `find_method`) in
a custom meta-object); the thread must be suspended at the point this request
is issued, and will be returned to suspended state again after the lookup has
taken place. Note that breakpoints may be hit and will be fired during this
operation.

    {
        "type": 35,
        "id": $id,
        "thread": 1,
        "handle": 1234,
        "name": "frobify",
    }

### Invoke (36)

Used by the client to invoke an object that it has a handle to, which should
be some kind of code object. The arguments may be natives or other objects
that the client has a handle for. The results will be returned in an Invoke
Result message. A target thread to perform this operation on is required. The
thread must be suspended at the point this request is issued, and will be
returned to suspended state again after the lookup has taken place. Note that
breakpoints may be hit and will be fired during this operation.

Named arguments require a "name" entry in the argument's map that gives a string.

    {
        "type": 36,
        "id": $id,
        "thread": 1,
        "handle": 1235,
        "arguments": [
            {
                "kind": "obj",
                "handle": 1234
            },
            {
                "kind": "str",
                "value": "Bulgogi"
            }
        ]
    }

### Invoke Result (37)

Contains the result of an Invoke message. If the result was of an object type
then a handle to it will be returned. If the invoke resulted in an exception,
then the `crashed` key will be set to a true value, and the `result` handle
will point to the exception object instead. Object result example:

    {
        "type": 37,
        "id": $id,
        "crashed": false,
        "kind": "obj",
        "handle": 1234,
        "obj_type": "Int",
        "concrete": true,
        "container": false
    }

Native int result example:

    {
        "type": 37,
        "id": $id,
        "crashed": false,
        "kind": "int",
        "value": 42
    }

Exception result:

    {
        "type": 37,
        "id": $id,
        "crashed": true,
        "kind": "obj",
        "handle": 1234,
        "obj_type": "X::AdHoc",
        "concrete": true,
        "container": false
    }

### Unhandled Exception (38)

This message is sent by MoarVM when an unhandled exception occurs. All threads
will be suspended. A handle to the exception object is included, together with
the thread it occurred on and the stack trace of that thread. So far as it is
able to do so, MoarVM will allow operations such as introspecting the context,
resolving methods, decontainerizing values, and invoking code.

    {
        "type": 38,
        "id": $id,
        "thread": 1,
        "handle": 1234,
        "frames": [
            {
                "file": "path/to/source/file",
                "line": 22,
                "bytecode_file": "path/to/bytecode/file",
                "name": "some-method",
                "type": "Method"
            },
            {
                "file": "path/to/source/file",
                "line": 12,
                "bytecode_file": "path/to/bytecode/file",
                "name": "",
                "type": "Block"
            }
        ]
    }

### Operation Unsuccessful (39)

A generic message sent by MoarVM if something went wrong while handling a
request.  This message is not in current use; Error Processing Message (1)
will be sent instead (as that includes a reason message).

    {
        "type": 39,
        "id": $id
    }

### Object Metadata Request (40)

Used by the client to get additional information about an object that goes
beyond its actual attributes. Can include miscellaneous details from the
REPRData and the object's internal state if it's concrete.

Additionally, all objects that have positional, associative, or attribute
features will point that out in their response.

    {
        "type": 40,
        "id": $id,
        "handle": 1234
    }

### Object Metadata Response (41)

Contains the results of introspecting the metadata of an object.

Every object has `reprname`. All concrete objects have `size` and
`unmanaged_size` fields.

Objects also include `positional_elems` and `associative_elems`
for objects that have positional and/or associative features.

`pos_features`, `ass_features`, and `attr_features` inform the client
which of the requests 42 ("Object Positionals Request"), 44
("Object Associatives Request"), or 32 ("Object Attributes Request")
will give useful results.

    {
        "type": 41,
        "id": $id,
        "metadata": {
            "reprname": "VMArray",
            "size": 128,
            "unmanaged_size": 1024,

            "vmarray_slot_type": "num32",
            "vmarray_elem_size": 4,
            "vmarray_allocated": 128,
            "vmarray_offset": 40,

            "positional_elems": 12,

            "pos_features": true,
            "ass_features": false,
            "attr_features": false,
        },
    }

### Object Positionals Request (42)

Used by the client to get the contents of an object that has
positional features, like an array.

    {
        "type": 42,
        "id": $id,
        "handle": 12345
    }

### Object Positionals Response (43)

The `kind` field can be "int", "num", "str" for native arrays,
or "obj" for object arrays.

In the case of an object array, every entry in the `contents`
field will be a map with keys `type`, `handle`, `concrete`,
and `container`.

For native arrays, the array contents are sent as their
corresponding messagepack types.

Native contents:

    {
        "type": 43,
        "id": $id,
        "kind": "int",
        "start": 0,
        "contents": [
            1, 2, 3, 4, 5, 6
        ]
    }

Object contents:

    {
        "type": 43,
        "id": $id,
        "kind": "obj",
        "start": 0,
        "contents": [
            {
                "type": "Potato",
                "handle": 9999,
                "concrete": true,
                "container": false
            },
            {
                "type": "Noodles",
                "handle": 10000,
                "concrete": false,
                "container": false
             }
         ]
     }

### Object Associatives Request (44)

Used by the client to get the contents of an object that has
associative features, like a hash.

    {
        "type": 44,
        "id": $id,
        "handle": 12345
    }

### Object Associatives Response (45)

All associative `contents` are of `kind` "obj", and are sent as an outer map
with string keys.  Each outer value is an inner map with `type`, `handle`,
`concrete`, and `container` keys, similar to Object Positionals Response (43).

    {
        "type": 45,
        "id": $id,
        "kind": "obj"
        "contents": {
            "Hello": {
                "type": "Poodle",
                "handle": 4242,
                "concrete": true,
                "container": false
            },
            "Goodbye": {
                "type": "Poodle",
                "handle": 4242,
                "concrete": true,
                "container": false
            }
        }
    }

### Handle Equivalence Request (46)

Ask the debugserver to check if handles refer to the same object.

    {
        "type": 46,
        "id": $id,
        "handles": [
            1, 2, 3, 4, 5, 6, 7
        ]
    }

### Handle Equivalence Response (47)

For any object that is referred to by multiple handles from
the request, return a list of all the handles that belong to
the given object.

    {
        "type": 47,
        "id": $id,
        "classes": [
            [1, 3],
            [2, 5, 7]
        ]
    }

### HLL Symbol Request (48)

MoarVM features a mechanism for objects and types to be registered
with an HLL, for example "nqp" or "perl6" or "raku". This request
allows you to find the available HLLs, a given HLL's keys, and
the value for a given key.

The first two variants will result in an HLL Symbol Response, while
the third one will result in a Handle Result message.

Get all HLL names:

    {
        "type": 48,
        "id": $id,
    }

Get an HLL's symbol names:

    {
        "type": 48,
        "id": $id,
        "HLL": "nqp"
    }

Get the value for a symbol:

    {
        "type": 48,
        "id": $id,
        "HLL": "nqp",
        "name": "
    }

### HLL Symbol Response (49)

For cases where the HLL Symbol Request results in a list of strings, i.e.
when all HLL names or an HLL's symbols are requested, the HLL Symbol
Response will be emitted.

    {
        "type": 49,
        "id": $id,
        "keys": [
            "one",
            "two",
        ]
    }
