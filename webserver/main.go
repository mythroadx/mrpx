package main

import (
	"embed"
	"errors"
	"flag"
	"io/fs"
	"log"
	"net/http"
	"strings"
	"time"
)

//go:embed all:site
var assets embed.FS

var version = "dev"

func siteHandler(site fs.FS) http.Handler {
	files := http.FileServer(http.FS(site))
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("X-Content-Type-Options", "nosniff")
		if r.Method != http.MethodGet && r.Method != http.MethodHead {
			w.Header().Set("Allow", "GET, HEAD")
			http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
			return
		}
		if r.URL.Path == "/" {
			target := "/main.html"
			if r.URL.RawQuery != "" {
				target += "?" + r.URL.RawQuery
			}
			http.Redirect(w, r, target, http.StatusTemporaryRedirect)
			return
		}
		name := strings.TrimPrefix(r.URL.Path, "/")
		if !fs.ValidPath(name) {
			http.NotFound(w, r)
			return
		}
		for _, part := range strings.Split(name, "/") {
			if strings.HasPrefix(part, ".") {
				http.NotFound(w, r)
				return
			}
		}
		info, err := fs.Stat(site, name)
		if err != nil {
			if errors.Is(err, fs.ErrNotExist) {
				http.NotFound(w, r)
			} else {
				log.Printf("read asset %q: %v", name, err)
				http.Error(w, "asset unavailable", http.StatusInternalServerError)
			}
			return
		}
		if info.IsDir() {
			http.NotFound(w, r)
			return
		}
		files.ServeHTTP(w, r)
	})
}

func main() {
	listen := flag.String("listen", "127.0.0.1:8080", "HTTP listen address")
	flag.Parse()
	site, err := fs.Sub(assets, "site")
	if err != nil {
		log.Fatal(err)
	}
	for _, name := range []string{"main.html", "mrpx.js", "mrpx.wasm", "runtime/manifest.json",
		"vendor/spessasynth/index.js", "vendor/spessasynth/GeneralUser-GS.sf2",
		"catalog.js", "catalog/catalog.json"} {
		if _, err := fs.Stat(site, name); err != nil {
			log.Fatalf("missing embedded asset %s: %v; build with make package VERSION=vX.Y.Z PLATFORM=wasm", name, err)
		}
	}
	server := &http.Server{
		Addr:              *listen,
		Handler:           siteHandler(site),
		ReadHeaderTimeout: 5 * time.Second,
		IdleTimeout:       60 * time.Second,
	}
	log.Printf("mrpx %s: http://%s", version, *listen)
	if err := server.ListenAndServe(); err != nil {
		log.Fatal(err)
	}
}
