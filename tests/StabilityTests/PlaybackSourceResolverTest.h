#pragma once

// Exercises the redirect walk that runs before libmpv opens a network source,
// against a pair of loopback servers standing in for a source host and the CDN
// it redirects to. Requires an initialized apartment and Winsock is started by
// the fixture itself.
void RunPlaybackSourceResolverTest();
