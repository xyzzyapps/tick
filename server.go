package main

import (
	"fmt"
	"net/http"
	"os"
	"path/filepath"
)

func main() {
	port := "8080"
	if len(os.Args) > 1 {
		port = os.Args[1]
	}

	webDir := "."
	if _, err := os.Stat("web"); err == nil {
		webDir = "web"
	}

	absDir, _ := filepath.Abs(webDir)
	fmt.Printf("🚀 Serving Tick Web Playground at http://localhost:%s/\n", port)
	fmt.Printf("📂 Root: %s\n", absDir)
	fmt.Println("Press Ctrl+C to stop.")

	fs := http.FileServer(http.Dir(webDir))
	http.Handle("/", http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		// Ensure proper MIME type for .wasm files
		if filepath.Ext(r.URL.Path) == ".wasm" {
			w.Header().Set("Content-Type", "application/wasm")
		}
		fs.ServeHTTP(w, r)
	}))

	if err := http.ListenAndServe(":"+port, nil); err != nil {
		fmt.Fprintf(os.Stderr, "Server error: %v\n", err)
	}
}
