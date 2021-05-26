// Copyright (c) 2021 Cloudflare, Inc.

package cf

// HeaderProcessor is called at various points while reading the client's
// request header from the wire.
type HeaderProcessor interface {
	// XXX Declare methods that will be called by
	// net/textproto.CFReadMIMEHeader.
	HeaderRaw(key []byte)
	HeaderCanonical(key string)
}

// HeaderProcessorContextKey is the key type for the request processor
// added to the request context.
type HeaderProcessorContextKey string
