# NTL RPC caller security test

This fixture verifies that NTL captures the original I/O requester's security
context before an asynchronous method moves to a system worker thread.

The test covers:

- user-mode requestor mode and original process ID;
- a referenced subject context surviving asynchronous dispatch;
- a restricted impersonation token enforced through an already-open endpoint;
- method authorization before request deserialization and callback execution;
- `call_context::check_access()` with allow and deny security descriptors;
- denied methods never reaching their application callback; and
- the same fixed-width contract from x64 and x86 clients.

The process ID is diagnostic metadata, not an authentication credential. Method
authorization uses the captured Windows subject context, an Authenticated
Users DACL, and the security reference monitor instead. The fixture treats
`SECURITY_SUBJECT_CONTEXT` as opaque and does not inspect its members.
