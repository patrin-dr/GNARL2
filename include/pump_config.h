#ifndef PUMP_CONFIG_H
#define PUMP_CONFIG_H

// ВАЖНО: не коммитьте сюда реальный серийный номер помпы!
// Реальный серийник задаётся при сборке в GitHub Actions
// (workflow input pump_id или repository secret PUMP_ID),
// workflow перезаписывает этот файл автоматически.
// Значения ниже — плейсхолдер для локальных/тестовых сборок.

#define PUMP_ID		"000000"	// pump serial number (placeholder - set via CI)
#define PUMP_FREQUENCY	868250000	// working pump frequency - 868.25 MHz

#endif
