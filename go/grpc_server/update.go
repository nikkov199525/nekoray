package grpc_server

import (
	"context"
	"encoding/json"
	"fmt"
	"grpc_server/gen"
	"io"
	"net/http"
	"os"
	"regexp"
	"runtime"
	"strconv"
	"strings"
	"time"

	"github.com/matsuridayo/libneko/neko_common"
)

const updateReleasesURL = "https://api.github.com/repos/nikkov199525/nekoray/releases?per_page=30"

var (
	updateDownloadURL  string
	updateDownloadName string
	forkVersionPattern = regexp.MustCompile(`(?i)^v?([0-9]+(?:\.[0-9]+)*)fork-([0-9]{4})\.([0-9]{1,2})\.([0-9]{1,2})$`)
	numberPattern      = regexp.MustCompile(`[0-9]+`)
)

type githubAsset struct {
	Name               string `json:"name"`
	BrowserDownloadURL string `json:"browser_download_url"`
}

type githubRelease struct {
	HTMLURL    string        `json:"html_url"`
	TagName    string        `json:"tag_name"`
	Assets     []githubAsset `json:"assets"`
	Draft      bool          `json:"draft"`
	Prerelease bool          `json:"prerelease"`
	Body       string        `json:"body"`
}

type updateCandidate struct {
	Release githubRelease
	Asset   githubAsset
	Version string
}

func versionNumbers(version string) []int {
	version = strings.TrimPrefix(strings.TrimSpace(version), "nekoray-")
	if match := forkVersionPattern.FindStringSubmatch(version); match != nil {
		parts := strings.Split(match[1], ".")
		numbers := make([]int, 0, len(parts)+3)
		for _, part := range parts {
			n, _ := strconv.Atoi(part)
			numbers = append(numbers, n)
		}
		year, _ := strconv.Atoi(match[2])
		day, _ := strconv.Atoi(match[3])
		month, _ := strconv.Atoi(match[4])
		// Fork versions use YYYY.DD.MM; compare them chronologically.
		return append(numbers, year, month, day)
	}

	parts := numberPattern.FindAllString(version, -1)
	numbers := make([]int, 0, len(parts))
	for _, part := range parts {
		n, _ := strconv.Atoi(part)
		numbers = append(numbers, n)
	}
	return numbers
}

func compareVersions(left, right string) int {
	a := versionNumbers(left)
	b := versionNumbers(right)
	length := len(a)
	if len(b) > length {
		length = len(b)
	}
	for i := 0; i < length; i++ {
		var av, bv int
		if i < len(a) {
			av = a[i]
		}
		if i < len(b) {
			bv = b[i]
		}
		if av < bv {
			return -1
		}
		if av > bv {
			return 1
		}
	}
	return 0
}

func assetVersion(name, platform string) (string, bool) {
	lowerName := strings.ToLower(name)
	if !strings.HasPrefix(lowerName, "nekoray-") {
		return "", false
	}
	for _, extension := range []string{".tar.gz", ".zip"} {
		if !strings.HasSuffix(lowerName, extension) {
			continue
		}
		base := name[:len(name)-len(extension)]
		suffix := "-" + platform
		if !strings.HasSuffix(strings.ToLower(base), suffix) {
			return "", false
		}
		return base[len("nekoray-") : len(base)-len(suffix)], true
	}
	return "", false
}

func findUpdate(releases []githubRelease, currentVersion, platform string, includePrerelease bool) (updateCandidate, bool) {
	var best updateCandidate
	found := false
	for _, release := range releases {
		if release.Draft || (release.Prerelease && !includePrerelease) {
			continue
		}
		for _, asset := range release.Assets {
			version, ok := assetVersion(asset.Name, platform)
			if !ok || compareVersions(version, currentVersion) <= 0 {
				continue
			}
			if !found || compareVersions(version, best.Version) > 0 {
				best = updateCandidate{Release: release, Asset: asset, Version: version}
				found = true
			}
		}
	}
	return best, found
}

func updatePlatform() (string, bool) {
	if runtime.GOOS == "windows" && runtime.GOARCH == "amd64" {
		return "windows64", true
	}
	if runtime.GOOS == "linux" && runtime.GOARCH == "amd64" {
		return "linux64", true
	}
	if runtime.GOOS == "darwin" {
		return "macos-" + runtime.GOARCH, true
	}
	return "", false
}

func githubRequest(ctx context.Context, url string) (*http.Request, error) {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("Accept", "application/vnd.github+json")
	req.Header.Set("User-Agent", "nekoray-fork-updater")
	return req, nil
}

func (s *BaseServer) Update(ctx context.Context, in *gen.UpdateReq) (*gen.UpdateResp, error) {
	ret := &gen.UpdateResp{}
	client := neko_common.CreateProxyHttpClient(neko_common.GetCurrentInstance())

	if in.Action == gen.UpdateAction_Check {
		checkContext, cancel := context.WithTimeout(ctx, 10*time.Second)
		defer cancel()

		platform, supported := updatePlatform()
		if !supported {
			ret.Error = "Not official support platform"
			return ret, nil
		}
		req, err := githubRequest(checkContext, updateReleasesURL)
		if err != nil {
			ret.Error = err.Error()
			return ret, nil
		}
		resp, err := client.Do(req)
		if err != nil {
			ret.Error = err.Error()
			return ret, nil
		}
		defer resp.Body.Close()
		if resp.StatusCode != http.StatusOK {
			ret.Error = fmt.Sprintf("GitHub releases request failed: %s", resp.Status)
			return ret, nil
		}

		var releases []githubRelease
		if err = json.NewDecoder(resp.Body).Decode(&releases); err != nil {
			ret.Error = err.Error()
			return ret, nil
		}
		currentVersion := strings.TrimPrefix(neko_common.Version_neko, "nekoray-")
		candidate, found := findUpdate(releases, currentVersion, platform, in.CheckPreRelease)
		if !found {
			updateDownloadURL = ""
			updateDownloadName = ""
			return ret, nil
		}

		updateDownloadURL = candidate.Asset.BrowserDownloadURL
		updateDownloadName = candidate.Asset.Name
		ret.AssetsName = candidate.Asset.Name
		ret.DownloadUrl = candidate.Asset.BrowserDownloadURL
		ret.ReleaseUrl = candidate.Release.HTMLURL
		ret.ReleaseNote = candidate.Release.Body
		ret.IsPreRelease = candidate.Release.Prerelease
		return ret, nil
	}

	if updateDownloadURL == "" {
		ret.Error = "No checked update is available"
		return ret, nil
	}
	req, err := githubRequest(ctx, updateDownloadURL)
	if err != nil {
		ret.Error = err.Error()
		return ret, nil
	}
	resp, err := client.Do(req)
	if err != nil {
		ret.Error = err.Error()
		return ret, nil
	}
	defer resp.Body.Close()
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		ret.Error = fmt.Sprintf("Update download failed: %s", resp.Status)
		return ret, nil
	}

	packagePath := "../nekoray.zip"
	if strings.HasSuffix(strings.ToLower(updateDownloadName), ".tar.gz") {
		packagePath = "../nekoray.tar.gz"
	}
	f, err := os.OpenFile(packagePath, os.O_TRUNC|os.O_CREATE|os.O_RDWR, 0644)
	if err != nil {
		ret.Error = err.Error()
		return ret, nil
	}
	defer f.Close()
	if _, err = io.Copy(f, resp.Body); err != nil {
		ret.Error = err.Error()
		return ret, nil
	}
	if err = f.Sync(); err != nil {
		ret.Error = err.Error()
	}
	return ret, nil
}
