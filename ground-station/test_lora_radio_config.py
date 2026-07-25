import backend_server
import lora_radio
from lora_radio import RFM95Radio


class RegisterRadio(RFM95Radio):
    def __init__(self):
        super().__init__()
        self.registers = {}
        self.writes = []

    def read_version(self):
        return 0x12

    def read_reg(self, address):
        return self.registers.get(address, 0)

    def write_reg(self, address, value):
        self.registers[address] = value
        self.writes.append((address, value))


def test_ground_defaults_match_flight_profile():
    radio = RFM95Radio()

    assert radio.frequency_hz == 869_525_000
    assert radio.spreading_factor == 8
    assert radio.sync_word == 0x12
    assert radio.tx_power_dbm == 17
    assert backend_server.CONFIG.frequency_hz == radio.frequency_hz
    assert backend_server.CONFIG.spreading_factor == radio.spreading_factor
    assert backend_server.CONFIG.sync_word == radio.sync_word
    assert backend_server.CONFIG.tx_power_dbm == radio.tx_power_dbm


def test_ground_register_configuration(monkeypatch):
    monkeypatch.setattr(lora_radio.time, "sleep", lambda _delay: None)
    radio = RegisterRadio()

    radio.configure_common()

    assert radio.registers[lora_radio.REG_FRF_MSB] == 0xD9
    assert radio.registers[lora_radio.REG_FRF_MID] == 0x61
    assert radio.registers[lora_radio.REG_FRF_LSB] == 0x99
    assert radio.registers[lora_radio.REG_MODEM_CONFIG_1] == 0x72
    assert radio.registers[lora_radio.REG_MODEM_CONFIG_2] == 0x84
    assert radio.registers[lora_radio.REG_MODEM_CONFIG_3] == 0x04
    assert radio.registers[lora_radio.REG_SYNC_WORD] == 0x12
    assert radio.registers[lora_radio.REG_PA_DAC] == 0x84
    assert radio.registers[lora_radio.REG_PA_CONFIG] == 0x8F
    assert radio.writes.index((lora_radio.REG_PA_DAC, 0x84)) < radio.writes.index(
        (lora_radio.REG_PA_CONFIG, 0x8F)
    )
