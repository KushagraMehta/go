// Copyright (c) 2021 Cloudflare, Inc.

package http_test

import (
	"net/http"
	"net/http/httptest"
	"testing"
)

// Check that, if a CFRequestProcessor constructor is configured, then the
// request context propagates the request processor of the correct type.
func TestCF_HTTP1RequestProcessor(t *testing.T) {
	ch := make(chan *testRequestProcessor)

	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		k := http.CFRequestProcessorContextKey("cf-request-processor")
		v := r.Context().Value(k)

		go func() {
			p, _ := v.(*testRequestProcessor)
			ch <- p
		}()
	}))
	ts.Config.CFNewRequestProcessor = newTestRequestProcessor
	defer ts.Close()

	resp, err := http.Get(ts.URL)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()

	p := <-ch
	if p == nil {
		t.Fatal("processor not propagated")
	}
}

// Same test as above, except enable HTTP/2.
func TestCF_HTTP2RequestProcessor(t *testing.T) {
	ch := make(chan *testRequestProcessor)

	ts := httptest.NewUnstartedServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		k := http.CFRequestProcessorContextKey("cf-request-processor")
		v := r.Context().Value(k)

		go func() {
			p, _ := v.(*testRequestProcessor)
			ch <- p
		}()
	}))
	ts.EnableHTTP2 = true
	ts.Config.CFNewRequestProcessor = newTestRequestProcessor
	ts.StartTLS()
	defer ts.Close()

	tc := ts.Client()
	resp, err := tc.Get(ts.URL)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()

	p := <-ch
	if p == nil {
		t.Fatal("processor not propagated")
	}
}

// Check that the request context does not propagate a request processor if no
// constructor is configured.
func TestCF_NoRequestProcessor(t *testing.T) {
	ch := make(chan *testRequestProcessor)

	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		k := http.CFRequestProcessorContextKey("cf-request-processor")
		v := r.Context().Value(k)

		go func() {
			p, _ := v.(*testRequestProcessor)
			ch <- p
		}()
	}))
	defer ts.Close()

	resp, err := http.Get(ts.URL)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()

	p := <-ch
	if p != nil {
		t.Fatal("processor propagated; expected nil")
	}
}

type testRequestProcessor struct{}

func newTestRequestProcessor() http.CFRequestProcessor {
	return new(testRequestProcessor)
}

func (p *testRequestProcessor) RequestLine(line []byte) {
	// no-op
}

func (p *testRequestProcessor) Header(key, value []byte) {
	// no-op
}
