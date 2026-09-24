"""Outbound HTTPS guard shared by the Design2UMG Python tools."""
import ipaddress
import socket
import urllib.parse
import urllib.request


def assert_remote_https_url(url: str) -> None:
    """Outbound fetches are remote HTTPS only. The hostname must resolve to
    public addresses; loopback, private, link-local and reserved targets are
    rejected so fetched content cannot be aimed at local or internal services."""
    parsed = urllib.parse.urlparse(url)
    if parsed.scheme != "https" or not parsed.hostname:
        raise ValueError(f"Refusing non-https fetch URL: {url}")
    hostname = parsed.hostname
    if hostname == "localhost":
        raise ValueError(f"Refusing local-network fetch URL: {url}")
    try:
        addrinfos = socket.getaddrinfo(hostname, 443, proto=socket.IPPROTO_TCP)
    except socket.gaierror as exc:
        raise ValueError(f"Cannot resolve fetch host {hostname}: {exc}") from exc
    for addrinfo in addrinfos:
        address = ipaddress.ip_address(addrinfo[4][0])
        if (
            address.is_private
            or address.is_loopback
            or address.is_link_local
            or address.is_reserved
            or address.is_multicast
        ):
            raise ValueError(f"Refusing non-public fetch target {address} for {url}")


class GuardedHTTPSHandler(urllib.request.HTTPSHandler):
    """Enforces the remote-HTTPS boundary at the transport layer: every request
    the opener executes (including redirect targets it re-opens) passes the URL
    guard, and cleartext HTTP is refused outright."""

    def http_open(self, req):
        raise ValueError(f"Refusing non-https fetch URL: {req.full_url}")

    def https_open(self, req):
        assert_remote_https_url(req.full_url)
        return super().https_open(req)


def build_guarded_opener() -> urllib.request.OpenerDirector:
    return urllib.request.build_opener(GuardedHTTPSHandler)
