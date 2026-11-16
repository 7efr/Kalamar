export type ShiftMode = "normal" | "big_run" | "eggstra_work";

export type SalmonRunRank =
  | "apprentice" | "part_timer" | "go_getter" | "overachiever"
  | "profreshional" | "profreshional_plus_0" | "profreshional_plus_1"
  | "profreshional_plus_2" | "profreshional_plus_3" | "eggsecutive_vp";

export type SalmonRunStage =
  | "spawning_grounds" | "sockeye_station" | "marooner_bay"
  | "gone_fission_hydroplant" | "jammin_salmon_junction";

export type SpecialWaveType =
  | "cohock_charge" | "griller" | "fog" | "rush"
  | "mothership" | "goldie_seeking" | "mudmouth_eruptions";

export interface WeaponLoadout {
  player_id: string;
  weapon_id: number;
  is_random: boolean;
}

export interface GearReward {
  gear_type: "head" | "clothes" | "shoes";
  gear_name: string;
  ability: string;
}

export interface ShiftJoinRequest {
  type: "shift_join";
  player_id: string;
  rank: SalmonRunRank;
  hazard_preference: number;
  mode: ShiftMode;
}

export interface ShiftSession {
  type: "shift_session_created";
  session_id: string;
  mode: ShiftMode;
  stage: SalmonRunStage;
  hazard_level: number;
  player_ids: string[];
  host_player_id: string;
  weapons: WeaponLoadout[];
  wave_quotas: [number, number, number];
  shift_start_at: string;
}

export interface ShiftWaveResult {
  type: "shift_wave_result";
  session_id: string;
  wave: 1 | 2 | 3;
  eggs_collected: number;
  quota: number;
  passed: boolean;
  special_wave: SpecialWaveType | null;
}

export interface ShiftResult {
  type: "shift_result";
  session_id: string;
  outcome: "clear" | "wipe_out";
  waves_cleared: 0 | 1 | 2 | 3;
  total_eggs: number;
  hazard_level: number;
  rank_delta: number;
  grizzco_points: number;
  gear_reward: GearReward | null;
}

export interface ShiftHeartbeat {
  type: "shift_heartbeat";
  session_id: string;
  wave: 1 | 2 | 3;
  eggs_banked: number;
  alive_count: number;
}

export interface QueueSizeUpdate {
  type: "queue_size_update";
  size: number;
}

export interface QueueError {
  type: "queue_error";
  reason: string;
}
