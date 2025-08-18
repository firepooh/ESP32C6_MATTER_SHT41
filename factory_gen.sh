#!/bin/bash
echo "Generate Factory Data"

export MATTER_SDK_PATH=$ESP_MATTER_PATH/connectedhomeip/connectedhomeip

esp-matter-mfg-tool -n 10 -cn "SensorBasic" -v 0xFFF2 -p 0x8001 --pai \
    -k $MATTER_SDK_PATH/credentials/test/attestation/Chip-Test-PAI-FFF2-8001-Key.pem \
    -c $MATTER_SDK_PATH/credentials/test/attestation/Chip-Test-PAI-FFF2-8001-Cert.pem \
    -cd $MATTER_SDK_PATH/credentials/test/certification-declaration/Chip-Test-CD-FFF2-8001.der