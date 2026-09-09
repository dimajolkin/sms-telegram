// HTTP→HTTPS relay for ESP (TinyGo/espradio has no software TLS).
//
//	go run ./cmd/tg-relay
//
// ESP: make flash RELAY_URL=http://<mac-lan-ip>:18765
package main

import (
	"io"
	"log"
	"net/http"
	"net/http/httputil"
	"net/url"
	"os"
	"strings"
	"time"
)

func main() {
	addr := envOr("LISTEN", ":18765")
	upstream, err := url.Parse("https://api.telegram.org")
	if err != nil {
		log.Fatal(err)
	}

	proxy := httputil.NewSingleHostReverseProxy(upstream)
	proxy.Transport = &http.Transport{
		Proxy:                 http.ProxyFromEnvironment,
		ResponseHeaderTimeout: 90 * time.Second,
		IdleConnTimeout:       90 * time.Second,
	}
	orig := proxy.Director
	proxy.Director = func(r *http.Request) {
		orig(r)
		r.Host = upstream.Host
		r.Header.Set("X-Forwarded-Proto", "https")
	}
	proxy.ErrorHandler = func(w http.ResponseWriter, r *http.Request, err error) {
		log.Printf("proxy error %s: %v", r.URL.Path, err)
		http.Error(w, "relay upstream error", http.StatusBadGateway)
	}

	mux := http.NewServeMux()
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/" || r.URL.Path == "/health" {
			io.WriteString(w, "tg-relay ok\n")
			return
		}
		if !strings.HasPrefix(r.URL.Path, "/bot") {
			http.NotFound(w, r)
			return
		}
		log.Printf("%s %s", r.Method, r.URL.RequestURI())
		proxy.ServeHTTP(w, r)
	})

	log.Printf("tg-relay listening on %s → %s", addr, upstream)
	log.Fatal(http.ListenAndServe(addr, mux))
}

func envOr(k, def string) string {
	if v := os.Getenv(k); v != "" {
		return v
	}
	return def
}
