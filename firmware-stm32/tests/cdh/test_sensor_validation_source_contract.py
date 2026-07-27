from pathlib import Path


FIRMWARE_ROOT = Path(__file__).resolve().parents[2]
HANDLER_SOURCE = (
    FIRMWARE_ROOT / 'Core' / 'Src' / 'cdh' / 'mpu6050_equipment_handler.c'
).read_text(encoding='utf-8')
HANDLER_HEADER = (
    FIRMWARE_ROOT / 'Core' / 'Inc' / 'cdh' / 'mpu6050_equipment_handler.h'
).read_text(encoding='utf-8')
VALIDATION_SOURCE = (
    FIRMWARE_ROOT / 'Core' / 'Src' / 'cdh' / 'sensor_validation.c'
).read_text(encoding='utf-8')
VALIDATION_HEADER = (
    FIRMWARE_ROOT / 'Core' / 'Inc' / 'cdh' / 'sensor_validation.h'
).read_text(encoding='utf-8')


def test_stationary_or_zero_g_state_is_not_an_imu_health_fault():
    combined = HANDLER_SOURCE + HANDLER_HEADER + VALIDATION_SOURCE + VALIDATION_HEADER

    assert 'MPU6050_IsDataFlatlined' not in combined
    assert 'MPU6050_FLATLINE_THRESHOLD' not in combined
    assert 'IMU_FLATLINE_THRESHOLD' not in combined
    assert 'IMU_FLATLINE_TIMEOUT_S' not in combined
    assert 'handler.imu_valid = 1;' in HANDLER_SOURCE


def test_frozen_imu_detection_requires_bit_identical_six_axis_output():
    compared_fields = (
        'imu_accel_x_g',
        'imu_accel_y_g',
        'imu_accel_z_g',
        'imu_gyro_x_dps',
        'imu_gyro_y_dps',
        'imu_gyro_z_dps',
    )

    for field in compared_fields:
        assert f'dp->{field} ==' in VALIDATION_SOURCE


def test_baro_gnss_crosscheck_restores_absolute_baro_altitude():
    assert 'scv->baro_ground_alt_cm == SCV_INVALID_I32' in VALIDATION_SOURCE
    assert 'dp->gps_fix_type != M10S_FIX_3D' in VALIDATION_SOURCE
    assert (
        'dp->baro_alt_m + ((float)scv->baro_ground_alt_cm / 100.0f)'
        in VALIDATION_SOURCE
    )
    assert 'fabsf(baro_pressure_alt_m - dp->gps_alt_m)' in VALIDATION_SOURCE
    assert 'validateBaroGpsCrossCheck(dp, scv);' in VALIDATION_SOURCE
