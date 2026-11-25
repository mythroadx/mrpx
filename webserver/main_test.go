package main

import (
	"net/http"
	"net/http/httptest"
	"testing"
	"testing/fstest"
)

func TestEmbeddedSiteRouting(t *testing.T) {
	site := fstest.MapFS{
		"main.html":                    {Data: []byte("<html>mrpx</html>")},
		"mrpx.wasm":                    {Data: []byte("\x00asm\x01\x00\x00\x00")},
		"runtime/app.mrp":              {Data: []byte("guest")},
		".gitkeep":                     {Data: []byte("hidden")},
		"runtime/.secret":              {Data: []byte("hidden")},
		"catalog/catalog.json":         {Data: []byte("{\"packages\":[]}")},
		"catalog/previews/example.png": {Data: []byte("\x89PNG\r\n\x1a\n")},
	}
	handler := siteHandler(site)
	for _, tc := range []struct {
		name, method, url string
		status            int
	}{
		{"entry", "GET", "/?f=runtime/app.mrp", http.StatusTemporaryRedirect},
		{"html", "GET", "/main.html", http.StatusOK},
		{"catalog", "GET", "/catalog/catalog.json", http.StatusOK},
		{"preview", "GET", "/catalog/previews/example.png", http.StatusOK},
		{"head", "HEAD", "/mrpx.wasm", http.StatusOK},
		{"directory", "GET", "/runtime", http.StatusNotFound},
		{"missing", "GET", "/missing", http.StatusNotFound},
		{"hidden", "GET", "/.gitkeep", http.StatusNotFound},
		{"nested hidden", "GET", "/runtime/.secret", http.StatusNotFound},
		{"traversal", "GET", "/../main.html", http.StatusNotFound},
		{"write", "POST", "/main.html", http.StatusMethodNotAllowed},
	} {
		t.Run(tc.name, func(t *testing.T) {
			response := httptest.NewRecorder()
			handler.ServeHTTP(response, httptest.NewRequest(tc.method, tc.url, nil))
			if response.Code != tc.status {
				t.Fatalf("status = %d, want %d", response.Code, tc.status)
			}
			if tc.name == "entry" && response.Header().Get("Location") != "/main.html?f=runtime/app.mrp" {
				t.Fatal("entry redirect discarded the MRP query")
			}
			if tc.name == "head" && (response.Body.Len() != 0 || response.Header().Get("Content-Type") != "application/wasm") {
				t.Fatal("WASM HEAD response has the wrong body or MIME type")
			}
		})
	}
}

func TestWASMRange(t *testing.T) {
	handler := siteHandler(fstest.MapFS{"mrpx.wasm": {Data: []byte("\x00asm\x01\x00\x00\x00")}})
	request := httptest.NewRequest("GET", "/mrpx.wasm", nil)
	request.Header.Set("Range", "bytes=0-3")
	response := httptest.NewRecorder()
	handler.ServeHTTP(response, request)
	if response.Code != http.StatusPartialContent || response.Body.String() != "\x00asm" ||
		response.Header().Get("Content-Range") != "bytes 0-3/8" {
		t.Fatal("WASM byte-range response is invalid")
	}
}
