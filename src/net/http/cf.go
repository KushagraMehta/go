// Copyright (c) 2021 Cloudflare, Inc.

package http

// CFRequestProcessor is called at various points while reading the client's
// request header from the wire.
type CFRequestProcessor interface {
	// RequestLine is called after parsing the request line.
	RequestLine(line []byte)
	// Header is called on each raw key/value pair after the header is parsed
	// but before the key is canonicalized.
	Header(key, value []byte)
}

// CFRequestProcessorContextKey is the key type for the request processor added
// to the request context.
type CFRequestProcessorContextKey string
