package main

import "os"

func setEnvDefault(name, value string) {
	if _, exists := os.LookupEnv(name); !exists {
		_ = os.Setenv(name, value)
	}
}

func enableLegacyConfigCompatibility() {
	// NekoRay still emits several legacy sing-box fields.
	// Keep them enabled so fresh sing-box (1.12+) can run existing configs.
	legacyEnv := []string{
		"ENABLE_DEPRECATED_BAD_MATCH_SOURCE",
		"ENABLE_DEPRECATED_GEOIP",
		"ENABLE_DEPRECATED_GEOSITE",
		"ENABLE_DEPRECATED_TUN_ADDRESS_X",
		"ENABLE_DEPRECATED_SPECIAL_OUTBOUNDS",
		"ENABLE_DEPRECATED_INBOUND_OPTIONS",
		"ENABLE_DEPRECATED_DESTINATION_OVERRIDE_FIELDS",
		"ENABLE_DEPRECATED_WIREGUARD_OUTBOUND",
		"ENABLE_DEPRECATED_WIREGUARD_GSO",
		"ENABLE_DEPRECATED_TUN_GSO",
		"ENABLE_DEPRECATED_LEGACY_DNS_SERVERS",
		"ENABLE_DEPRECATED_LEGACY_DNS_FAKEIP_OPTIONS",
		"ENABLE_DEPRECATED_OUTBOUND_DNS_RULE_ITEM",
		"ENABLE_DEPRECATED_MISSING_DOMAIN_RESOLVER",
		"ENABLE_DEPRECATED_LEGACY_ECH_OPTIONS",
		"ENABLE_DEPRECATED_LEGACY_DOMAIN_STRATEGY_OPTIONS",
	}
	for _, name := range legacyEnv {
		setEnvDefault(name, "true")
	}
}
