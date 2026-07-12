package recovery

import (
	"time"
)

// RecoveryModule represents a physical ESP32 Recovery Module registered in the Hub database.
type RecoveryModule struct {
	Id              string    `json:"id" db:"id"`
	Name            string    `json:"name" db:"name"`
	MacAddress      string    `json:"mac_address" db:"mac_address"`
	IpAddress       string    `json:"ip_address" db:"ip_address"`
	GatewayIp       string    `json:"gateway_ip" db:"gateway_ip"`
	GatewayName     string    `json:"gateway_name" db:"gateway_name"`
	MaxChannels     int       `json:"max_channels" db:"max_channels"`
	FirmwareVersion string    `json:"firmware_version" db:"firmware_version"`
	Status          string    `json:"status" db:"status"`
	ConfigRevision        int       `json:"config_revision" db:"config_revision"`
	ConfigHash            string    `json:"config_hash" db:"config_hash"`
	Temperature           float64   `json:"temperature" db:"temperature"`
	TempThresholdWarning  float64   `json:"temp_threshold_warning" db:"temp_threshold_warning"`
	TempThresholdCritical float64   `json:"temp_threshold_critical" db:"temp_threshold_critical"`
	Created               time.Time `json:"created" db:"created"`
	Updated               time.Time `json:"updated" db:"updated"`
}

// RecoveryChannel represents the configuration for a single physical channel on a module.
type RecoveryChannel struct {
	Id                string    `json:"id" db:"id"`
	Module            string    `json:"module" db:"module"`
	ChannelNumber     int       `json:"channel_number" db:"channel_number"`
	System            string    `json:"system" db:"system"`
	HostIp            string    `json:"host_ip" db:"host_ip"`
	ProbePorts        []int     `json:"probe_ports" db:"probe_ports"`
	FailureThreshold  int       `json:"failure_threshold" db:"failure_threshold"`
	BootGraceSeconds  int       `json:"boot_grace_seconds" db:"boot_grace_seconds"`
	Maintenance       bool      `json:"maintenance" db:"maintenance"`
	WolEnabled        bool      `json:"wol_enabled" db:"wol_enabled"`
	AutoWol           bool      `json:"auto_wol" db:"auto_wol"`
	MacAddress        string    `json:"mac_address" db:"mac_address"`
	BroadcastAddress  string    `json:"broadcast_address" db:"broadcast_address"`
	WolPort           int       `json:"wol_port" db:"wol_port"`
	Created           time.Time `json:"created" db:"created"`
	Updated           time.Time `json:"updated" db:"updated"`
}

// RecoveryEvent logs the history of server states, probing, and execution stages.
type RecoveryEvent struct {
	Id        string    `json:"id" db:"id"`
	System    string    `json:"system" db:"system"`
	Module    string    `json:"module" db:"module"`
	Channel   int       `json:"channel" db:"channel"`
	Event     string    `json:"event" db:"event"`
	Timestamp time.Time `json:"timestamp" db:"timestamp"`
	Metadata  any       `json:"metadata" db:"metadata"`
	Created   time.Time `json:"created" db:"created"`
	Updated   time.Time `json:"updated" db:"updated"`
}
