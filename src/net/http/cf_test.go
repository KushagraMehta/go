// Copyright (c) 2021 Cloudflare, Inc.

package http_test

import (
	"io"
	"net/http"
	"net/http/httptest"
	"testing"
)

// Check that, if a CFRequestProcessor constructor is configured, then the
// request context propagates the request processor of the correct type.
func TestCFRequestProcessor(t *testing.T) {
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		k := http.CFRequestProcessorContextKey("cf-request-processor")
		v := r.Context().Value(k)
		_, ok := v.(*testRequestProcessor)
		if v == nil || !ok {
			w.Write([]byte{0})
		} else {
			w.Write([]byte{1})
		}
	}))
	ts.Config.CFNewRequestProcessor = newTestRequestProcessor
	defer ts.Close()

	resp, err := http.Get(ts.URL)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()

	res, err := io.ReadAll(resp.Body)
	if err != nil {
		t.Fatal(err)
	}

	if res[0] != 1 {
		t.Fatal("Request context is missing request processor")
	}
}

// Check that the request context does not propagate a request processor if no
// constructor is configured.
func TestCFNoRequestProcessor(t *testing.T) {
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		k := http.CFRequestProcessorContextKey("cf-request-processor")
		v := r.Context().Value(k)
		if v == nil {
			w.Write([]byte{0})
		} else {
			w.Write([]byte{1})
		}
	}))
	defer ts.Close()

	resp, err := http.Get(ts.URL)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()

	res, err := io.ReadAll(resp.Body)
	if err != nil {
		t.Fatal(err)
	}

	if res[0] != 0 {
		t.Fatal("Request context has the request processor but none was expected")
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
