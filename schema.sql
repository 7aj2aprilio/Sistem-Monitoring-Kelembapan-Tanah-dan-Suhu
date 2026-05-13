-- Membuat Database
CREATE DATABASE IF NOT EXISTS iot_monitoring;
USE iot_monitoring;

-- Membuat Tabel sensor_data
CREATE TABLE IF NOT EXISTS sensor_data (
    id INT AUTO_INCREMENT PRIMARY KEY,
    suhu FLOAT NOT NULL,
    kelembaban_udara FLOAT NOT NULL,
    kelembaban_tanah INT NOT NULL,
    status_pompa TINYINT(1) NOT NULL COMMENT '1 = Menyala, 0 = Mati',
    mode VARCHAR(10) NOT NULL COMMENT 'auto / manual',
    waktu_simpan TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Contoh Query untuk melihat data terbaru
-- SELECT * FROM sensor_data ORDER BY waktu_simpan DESC LIMIT 10;
