#ifndef PUMP_CONFIG_H
#define PUMP_CONFIG_H

// Серийный номер помпы по умолчанию.
// Может быть переопределён при сборке в GitHub Actions
// (workflow input pump_id или repository secret PUMP_ID) -
// workflow перезаписывает этот файл автоматически.

#define PUMP_ID		"970260"	// pump serial number (default; overridable via CI)
#define PUMP_FREQUENCY	868250000	// working pump frequency - 868.25 MHz

#endif
