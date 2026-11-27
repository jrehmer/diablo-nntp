package e2e

import (
	"bytes"
	"encoding/binary"
	"os"
	"strings"
	"testing"
)

func TestReaderFutureOverviewVersion(t *testing.T) {
	st, spool := spoolStore(t)
	s := NewReaderSite(t, ReaderOptions{Spool: spool})
	id := msgID(t)
	feedReader(t, s, st, "e2e.test", newArticle(id, "e2e.test", "overview version", "body"))
	waitGroup(t, s, "211 1 1 1")

	readOverview := func(t *testing.T) []string {
		t.Helper()
		c := s.Reader(t)
		defer c.Close()
		c.ExpectCmd(211, "GROUP e2e.test")
		c.ExpectCmd(224, "OVER 1")
		rows := c.ReadDotLines()
		c.ExpectCmd(205, "QUIT")
		return rows
	}
	if rows := readOverview(t); len(rows) != 1 || !strings.Contains(rows[0], id) {
		t.Fatalf("version 4: OVER 1 gave %q, want one row for %s", rows, id)
	}

	path := overFilePath(t, s)
	s.Proc.Stop()
	original, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	if len(original) < 16 || binary.NativeEndian.Uint32(original) != 4 {
		t.Fatalf("expected a populated version 4 index, got %d bytes", len(original))
	}
	headSize := int(binary.NativeEndian.Uint32(original[8:]))
	if headSize < 16 || headSize >= len(original) {
		t.Fatalf("unexpected header size %d in a %d-byte populated index", headSize, len(original))
	}

	for _, tc := range []struct {
		name       string
		version    uint32
		headerOnly bool
		repair     bool
	}{
		{name: "future_populated", version: 5},
		{name: "future_header_only", version: 5, headerOnly: true},
		{name: "legacy_repair", version: 4, headerOnly: true, repair: true},
	} {
		t.Run(tc.name, func(t *testing.T) {
			data := bytes.Clone(original)
			if tc.headerOnly {
				data = data[:headSize]
			}
			binary.NativeEndian.PutUint32(data, tc.version)
			writeOver(t, path, data)
			restartReader(t, s)
			defer s.Proc.Stop()

			if rows := readOverview(t); len(rows) != 0 {
				t.Errorf("OVER 1 gave %q, want no rows", rows)
			}
			after, err := os.ReadFile(path)
			if err != nil {
				t.Fatal(err)
			}
			if tc.repair {
				if len(after) <= headSize || binary.NativeEndian.Uint32(after) != tc.version {
					t.Errorf("header-only version 4 index was not repaired into a populated index")
				}
			} else if !bytes.Equal(after, data) {
				t.Errorf("version %d index was rewritten (%d bytes before, %d after)",
					tc.version, len(data), len(after))
			}
		})
	}
}
