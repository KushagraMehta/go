// Copyright (c) 2021 Cloudflare, Inc.

package cf_test

import (
	"net/cf"
	"net/http"
	"net/http/httptest"
	"testing"
)

// Check that, if a net/cf.HeaderProcessor constructor is configured, then the
// request context propagates the request processor of the correct type.
func TestHTTP1HeaderProcessor(t *testing.T) {
	ch := make(chan *testHeaderProcessor)

	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		k := cf.HeaderProcessorContextKey("cf-header-processor")
		v := r.Context().Value(k)

		go func() {
			p, _ := v.(*testHeaderProcessor)
			ch <- p
		}()
	}))
	ts.Config.CFNewHeaderProcessor = newTestHeaderProcessor
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
func TestHTTP2HeaderProcessor(t *testing.T) {
	ch := make(chan *testHeaderProcessor)

	ts := httptest.NewUnstartedServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		k := cf.HeaderProcessorContextKey("cf-header-processor")
		v := r.Context().Value(k)

		go func() {
			p, _ := v.(*testHeaderProcessor)
			ch <- p
		}()
	}))
	ts.EnableHTTP2 = true
	ts.Config.CFNewHeaderProcessor = newTestHeaderProcessor
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
func TestNoHeaderProcessor(t *testing.T) {
	ch := make(chan *testHeaderProcessor)

	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		k := cf.HeaderProcessorContextKey("cf-header-processor")
		v := r.Context().Value(k)

		go func() {
			p, _ := v.(*testHeaderProcessor)
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

type testHeaderProcessor struct{}

func newTestHeaderProcessor() cf.HeaderProcessor {
	return new(testHeaderProcessor)
}

func (p *testHeaderProcessor) HeaderRaw(key []byte) {
	// no-op
}

func (p *testHeaderProcessor) HeaderCanonical(key string) {
	// no-op
}
