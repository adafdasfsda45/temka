using Newtonsoft.Json;
using Newtonsoft.Json.Linq;
using Oxide.Core;
using Oxide.Core.Libraries;
using Oxide.Core.Libraries.Covalence;
using Oxide.Core.Plugins;
using ProtoBuf;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.RegularExpressions;
using UnityEngine;

namespace Oxide.Plugins
{
    [Info("DarkAntiCheat", "darkkknes", "1.8.2")]
    [Description("Мета античит")]
    public class DarkAntiCheat : RustPlugin
    {
        private ConfigData config;
        private Timer checkTimer;
        private HashSet<BasePlayer> activePlayers = new HashSet<BasePlayer>();
        private Dictionary<ulong, float> mountTime = new Dictionary<ulong, float>();
        private Dictionary<ulong, Timer> banTimers = new Dictionary<ulong, Timer>();
        private Dictionary<ulong, int> totalHits = new Dictionary<ulong, int>();
        private Dictionary<ulong, int> headHits = new Dictionary<ulong, int>();
        private Dictionary<ulong, int> hitboxViolations = new Dictionary<ulong, int>();
        private Dictionary<ulong, int> totalShots = new Dictionary<ulong, int>();
        private Dictionary<ulong, int> headShots = new Dictionary<ulong, int>();
        private Timer statsResetTimer;
        private Timer detectionsResetTimer;
        private Timer sprintCheckTimer;
        private Timer sprintViolationsResetTimer;
        private Timer jumpViolationsResetTimer;
        private Timer jumpResetTimer;
        private Dictionary<string, Dictionary<ulong, int>> aimOnEokaHitsPerTarget = new Dictionary<string, Dictionary<ulong, int>>();
        private Dictionary<string, Dictionary<ulong, int>> aimOnEokaHeadshotsPerTarget = new Dictionary<string, Dictionary<ulong, int>>();
        private Dictionary<ulong, int> sprintViolations = new Dictionary<ulong, int>();
        // Время до которого следует игнорировать детект спринта после слишком высокой скорости
        private Dictionary<ulong, float> sprintIgnoreUntil = new Dictionary<ulong, float>();
        private Dictionary<ulong, Vector3> lastPositions = new Dictionary<ulong, Vector3>();
        private Dictionary<ulong, float> lastCheckTimes = new Dictionary<ulong, float>();
        private Dictionary<ulong, float> lastSpeeds = new Dictionary<ulong, float>();
        private Dictionary<ulong, Vector3> lastAngles = new Dictionary<ulong, Vector3>();
        private Dictionary<ulong, JumpState> jumpStates = new Dictionary<ulong, JumpState>();
        private Dictionary<ulong, float> lastTransportExitTime = new Dictionary<ulong, float>();
        private Dictionary<ulong, float> waterWalkTime = new Dictionary<ulong, float>();
        private Dictionary<ulong, int> manipulatorViolations = new Dictionary<ulong, int>();
        private Dictionary<ulong, float> manipulatorSuspicionTime = new Dictionary<ulong, float>();
        private Timer manipulatorViolationsResetTimer;
        private Dictionary<ulong, float> medicalStartTimes = new Dictionary<ulong, float>();
        private Dictionary<ulong, int> medicalSpeedViolations = new Dictionary<ulong, int>();
        private Dictionary<ulong, float> lastHealthValues = new Dictionary<ulong, float>();
        private Dictionary<ulong, float> lastExternalHealTime = new Dictionary<ulong, float>();
        private Timer medicalSpeedViolationsResetTimer;
        private Dictionary<ulong, int> airShotViolations = new Dictionary<ulong, int>();
        private Timer airShotViolationsResetTimer;
        private Dictionary<ulong, float> airHoverStartTime = new Dictionary<ulong, float>();
        private Dictionary<ulong, Vector3> lastAirPosition = new Dictionary<ulong, Vector3>();
        private Dictionary<ulong, int> airHoverViolations = new Dictionary<ulong, int>();

        private class JumpState
        {
            public float LastGroundY;
            public float MaxJumpHeight;
            public bool WasInAir;
            public float LastJumpTime;
            public int JumpCount;
            public float LastLadderTime;
            public bool WasOnLadder;
            public int Violations;
            public float LastSpeedCheckTime;
            public float LastSpeed;
            public bool WasRunning;
            public float StartHealth;
        }

        class DetectionConfig
        {
            [JsonProperty("Банить?")]
            public bool Enabled { get; set; }

            [JsonProperty("Причина бана")]
            public string BanReason { get; set; }

            [JsonProperty("На сколько часов бан?")]
            public int BanHours { get; set; }

            [JsonProperty("Discord Webhook URL")]
            public string WebhookUrl { get; set; }

            [JsonProperty("Отправлять уведомление в Discord?")]
            public bool SendDiscordNotification { get; set; }

            [JsonProperty("Количество детектов для бана")]
            public int DetectsForBan { get; set; }
        }

        class MinicopterConfig : DetectionConfig
        {
            [JsonProperty("Задержка перед баном (в секундах)")]
            public float BanDelay { get; set; }
        }

        class LongRangeConfig : DetectionConfig
        {
            [JsonProperty("Минимальная дистанция для бана (в метрах)")]
            public float MinDistance { get; set; }
        }

        class HitboxConfig : DetectionConfig
        {
            [JsonProperty("Минимальное расстояние для запрета детекта (в метрах)")]
            public float MinDetectDistance { get; set; } = 7f;

            [JsonProperty("Процент попаданий в голову для детекта")]
            public float HeadshotPercentage { get; set; } = 100f;

            [JsonProperty("Интервал сброса статистики (в секундах)")]
            public float StatsResetInterval { get; set; } = 30f;

            [JsonProperty("Интервал сброса детектов (в секундах)")]
            public float DetectionsResetInterval { get; set; } = 300f;
        }

        class SprintingConfig : DetectionConfig
        {
            [JsonProperty("Максимальный порог скорости для детекта")]
            public float MaxSpeedThreshold { get; set; } = 10f;

            [JsonProperty("Интервал проверки (в секундах)")]
            public float CheckInterval { get; set; } = 0.05f;

            [JsonProperty("Время сброса нарушений (в секундах)")]
            public float ViolationResetTime { get; set; } = 180f;
        }

        class JumpDetectionConfig : DetectionConfig
        {
            [JsonProperty("Максимальная высота прыжка")]
            public float MaxJumpHeight { get; set; } = 2.8f;
        }

        class AimOnEokaConfig : DetectionConfig
        {
            [JsonProperty("Минимальное расстояние для детекта (в метрах)")]
            public float MinDistance { get; set; } = 5f;

            [JsonProperty("Количество попаданий для детекта")]
            public int TotalHitsForDetect { get; set; } = 20;

            [JsonProperty("Количество головных ударов для детекта")]
            public int HeadshotsForDetect { get; set; } = 5;

            [JsonProperty("Исключить зелёные пули")]
            public bool ExcludeGreenBullets { get; set; } = true;
        }

        class NailgunConfig : DetectionConfig
        {
            [JsonProperty("Минимальная дистанция для бана (в метрах)")]
            public float MinDistance { get; set; } = 50f;
        }

        class WaterWalkConfig : DetectionConfig
        {
            [JsonProperty("Минимальное время для детекта (в секундах)")]
            public float MinDetectTime { get; set; } = 3f;
        }

        class ManipulatorConfig : DetectionConfig
        {
            [JsonProperty("Минимальное расстояние смещения пули (в метрах)")]
            public float MinBulletOffset { get; set; } = 2f;

            [JsonProperty("Максимальное расстояние смещения пули (в метрах)")]
            public float MaxBulletOffset { get; set; } = 4f;
        }

        class MedicalSpeedConfig : DetectionConfig
        {
            [JsonProperty("Минимальное время использования шприца (в секундах)")]
            public float MinSyringeUseTime { get; set; } = 1.8f;

            [JsonProperty("Время сброса нарушений (в секундах)")]
            public float ViolationResetTime { get; set; } = 300f;
        }

        class AirShotConfig : DetectionConfig
        {
            [JsonProperty("Минимальное расстояние для детекта (в метрах)")]
            public float MinDistance { get; set; } = 5f;

            [JsonProperty("Минимальная высота для детекта")]
            public float MinHeight { get; set; } = 1f;
        }

        class NoFallDamageConfig : DetectionConfig
        {
            [JsonProperty("Минимальная высота падения для детекта (в метрах)")]
            public float MinFallHeight { get; set; } = 6f;
        }

        class ConfigData
        {
            [JsonProperty("Детект миникоптера")]
            public MinicopterConfig MinicopterDetection { get; set; }

            [JsonProperty("Детект дальности")]
            public LongRangeConfig LongRangeDetection { get; set; }

            [JsonProperty("Детект хитбоксов")]
            public HitboxConfig HitboxDetection { get; set; }

            [JsonProperty("Детект спринта")]
            public SprintingConfig SprintingDetection { get; set; }

            [JsonProperty("Детект прыжка")]
            public JumpDetectionConfig JumpDetection { get; set; }

            [JsonProperty("Детект аима на еоку")]
            public AimOnEokaConfig AimOnEokaDetection { get; set; }

            [JsonProperty("Детект аима на гвоздемёт")]
            public NailgunConfig NailgunDetection { get; set; }

            [JsonProperty("Детект ходьбы по воде")]
            public WaterWalkConfig WaterWalkDetection { get; set; }

            [JsonProperty("Детект манипулятора пули")]
            public ManipulatorConfig ManipulatorDetection { get; set; }

            [JsonProperty("Детект быстрого хила")]
            public MedicalSpeedConfig MedicalSpeedDetection { get; set; }

            [JsonProperty("Детект стрельбы в воздухе")]
            public AirShotConfig AirShotDetection { get; set; }

            [JsonProperty("Детект падения с большой высоты")]
            public NoFallDamageConfig NoFallDamageDetection { get; set; }
        }

        protected override void LoadConfig()
        {
            base.LoadConfig();
            try
            {
                config = Config.ReadObject<ConfigData>();
                if (config == null) LoadDefaultConfig();
                if (config.AimOnEokaDetection == null)
                {
                    config.AimOnEokaDetection = new AimOnEokaConfig();
                }
                // Убеждаемся, что раздел HitboxDetection присутствует и интервалы корректны
                if (config.HitboxDetection == null)
                {
                    config.HitboxDetection = new HitboxConfig();
                }

                if (config.HitboxDetection.StatsResetInterval <= 0f)
                {
                    config.HitboxDetection.StatsResetInterval = 30f;
                }

                if (config.HitboxDetection.DetectionsResetInterval <= 0f)
                {
                    config.HitboxDetection.DetectionsResetInterval = 300f;
                }

                // Проверяем наличие раздела детекта отсутствия урона от падения
                if (config.NoFallDamageDetection == null)
                {
                    config.NoFallDamageDetection = new NoFallDamageConfig { Enabled = true, MinFallHeight = 6f };
                }
            }
            catch
            {
                LoadDefaultConfig();
            }
            SaveConfig();
        }

        protected override void LoadDefaultConfig()
        {
            config = new ConfigData
            {
                MinicopterDetection = new MinicopterConfig
                {
                    Enabled = true,
                    BanReason = "Cheat Detected! (1)",
                    BanHours = 0,
                    BanDelay = 3.0f,
                    WebhookUrl = "https://discord.com/api/webhooks/1389619725193052191/8QXgEl0zaK4z3zuv_kHL1X2hteBkQqXWPqKCkaa7Nv8xxJFN8UUQPCYtH_syMjICap_m",
                    SendDiscordNotification = true,
                    DetectsForBan = 0
                },
                LongRangeDetection = new LongRangeConfig
                {
                    Enabled = true,
                    BanReason = "Cheat Detected! (2)",
                    BanHours = 0,
                    MinDistance = 500f,
                    WebhookUrl = "https://discord.com/api/webhooks/1389619725193052191/8QXgEl0zaK4z3zuv_kHL1X2hteBkQqXWPqKCkaa7Nv8xxJFN8UUQPCYtH_syMjICap_m",
                    SendDiscordNotification = true,
                    DetectsForBan = 0
                },
                HitboxDetection = new HitboxConfig
                {
                    Enabled = true,
                    BanReason = "Cheat Detected! (3)",
                    BanHours = 0,
                    WebhookUrl = "https://discord.com/api/webhooks/1389619725193052191/8QXgEl0zaK4z3zuv_kHL1X2hteBkQqXWPqKCkaa7Nv8xxJFN8UUQPCYtH_syMjICap_m",
                    SendDiscordNotification = true,
                    DetectsForBan = 25,
                    MinDetectDistance = 7f,
                    HeadshotPercentage = 100f,
                    StatsResetInterval = 30f,
                    DetectionsResetInterval = 300f
                },
                SprintingDetection = new SprintingConfig
                {
                    Enabled = true,
                    BanReason = "Cheat Detected! (4)",
                    BanHours = 0,
                    WebhookUrl = "https://discord.com/api/webhooks/1389619725193052191/8QXgEl0zaK4z3zuv_kHL1X2hteBkQqXWPqKCkaa7Nv8xxJFN8UUQPCYtH_syMjICap_m",
                    SendDiscordNotification = false,
                    DetectsForBan = 3,
                    MaxSpeedThreshold = 5f,
                    CheckInterval = 0.05f,
                    ViolationResetTime = 120f
                },
                JumpDetection = new JumpDetectionConfig
                {
                    Enabled = true,
                    BanReason = "Cheat Detected! (5)",
                    BanHours = 0,
                    WebhookUrl = "https://discord.com/api/webhooks/1389619725193052191/8QXgEl0zaK4z3zuv_kHL1X2hteBkQqXWPqKCkaa7Nv8xxJFN8UUQPCYtH_syMjICap_m",
                    SendDiscordNotification = true,
                    DetectsForBan = 11,
                    MaxJumpHeight = 2.8f
                },
                AimOnEokaDetection = new AimOnEokaConfig
                {
                    Enabled = true,
                    BanReason = "Cheat Detected! (6)",
                    BanHours = 0,
                    WebhookUrl = "https://discord.com/api/webhooks/1389619725193052191/8QXgEl0zaK4z3zuv_kHL1X2hteBkQqXWPqKCkaa7Nv8xxJFN8UUQPCYtH_syMjICap_m",
                    SendDiscordNotification = true,
                    DetectsForBan = 0,
                    MinDistance = 5f,
                    TotalHitsForDetect = 20,
                    HeadshotsForDetect = 5,
                    ExcludeGreenBullets = true
                },
                NailgunDetection = new NailgunConfig
                {
                    Enabled = true,
                    BanReason = "Cheat Detected! (7)",
                    BanHours = 0,
                    WebhookUrl = "https://discord.com/api/webhooks/1389619725193052191/8QXgEl0zaK4z3zuv_kHL1X2hteBkQqXWPqKCkaa7Nv8xxJFN8UUQPCYtH_syMjICap_m",
                    SendDiscordNotification = true,
                    DetectsForBan = 2,
                    MinDistance = 34f
                },
                WaterWalkDetection = new WaterWalkConfig
                {
                    Enabled = true,
                    BanReason = "Cheat Detected! (8)",
                    BanHours = 0,
                    WebhookUrl = "https://discord.com/api/webhooks/1389619725193052191/8QXgEl0zaK4z3zuv_kHL1X2hteBkQqXWPqKCkaa7Nv8xxJFN8UUQPCYtH_syMjICap_m",
                    SendDiscordNotification = true,
                    DetectsForBan = 0,
                    MinDetectTime = 3f
                },
                ManipulatorDetection = new ManipulatorConfig
                {
                    Enabled = true,
                    BanReason = "Cheat Detected! (9)",
                    BanHours = 0,
                    WebhookUrl = "https://discord.com/api/webhooks/1389619725193052191/8QXgEl0zaK4z3zuv_kHL1X2hteBkQqXWPqKCkaa7Nv8xxJFN8UUQPCYtH_syMjICap_m",
                    SendDiscordNotification = true,
                    DetectsForBan = 2,
                    MinBulletOffset = 3f,
                    MaxBulletOffset = 25f
                },
                MedicalSpeedDetection = new MedicalSpeedConfig
                {
                    Enabled = true,
                    BanReason = "Cheat Detected! (10)",
                    BanHours = 0,
                    WebhookUrl = "https://discord.com/api/webhooks/1389619725193052191/8QXgEl0zaK4z3zuv_kHL1X2hteBkQqXWPqKCkaa7Nv8xxJFN8UUQPCYtH_syMjICap_m",
                    SendDiscordNotification = true,
                    DetectsForBan = 0,
                    MinSyringeUseTime = 1.6f,
                    ViolationResetTime = 300f
                },
                AirShotDetection = new AirShotConfig
                {
                    Enabled = true,
                    BanReason = "Cheat Detected! (11)",
                    BanHours = 0,
                    WebhookUrl = "https://discord.com/api/webhooks/1389619725193052191/8QXgEl0zaK4z3zuv_kHL1X2hteBkQqXWPqKCkaa7Nv8xxJFN8UUQPCYtH_syMjICap_m",
                    SendDiscordNotification = true,
                    DetectsForBan = 12,
                    MinDistance = 2f,
                    MinHeight = 1.1f
                },
                NoFallDamageDetection = new NoFallDamageConfig
                {
                    Enabled = true,
                    BanReason = "Cheat Detected! (12)",
                    BanHours = 0,
                    WebhookUrl = "https://discord.com/api/webhooks/1389619725193052191/8QXgEl0zaK4z3zuv_kHL1X2hteBkQqXWPqKCkaa7Nv8xxJFN8UUQPCYtH_syMjICap_m",
                    SendDiscordNotification = true,
                    DetectsForBan = 0,
                    MinFallHeight = 6f
                }
            };
        }

        protected override void SaveConfig() => Config.WriteObject(config);

        private void Init()
        {
            LoadConfig();
            // игнорирование всего сразу
            permission.RegisterPermission("darkanticheat.ignore.all", this);
            // игнорирование детекта прыжков
            permission.RegisterPermission("darkanticheat.ignore.jump", this);
            // права на разбан
            permission.RegisterPermission("darkanticheat.unban", this);
            // игнорирование детекта ноуклипа - УДАЛЕНО
            // permission.RegisterPermission("darkanticheat.ignore.noclip", this);
            // игнорирование детекта оружия в миникоптере
            permission.RegisterPermission("darkanticheat.ignore.minicopter", this);
            // игнорирование детекта дальности
            permission.RegisterPermission("darkanticheat.ignore.longrange", this);
            // игнорирование детекта хитбоксов
            permission.RegisterPermission("darkanticheat.ignore.hitbox", this);
            // игнорирование детекта спринта
            permission.RegisterPermission("darkanticheat.ignore.sprint", this);
            // игнорирование детекта аима на еоку
            permission.RegisterPermission("darkanticheat.ignore.eoka", this);
            // игнорирование детекта гвоздемёта
            permission.RegisterPermission("darkanticheat.ignore.nailgun", this);
            // игнорирование детекта ходьбы по воде
            permission.RegisterPermission("darkanticheat.ignore.waterwalk", this);
            // игнорирование детекта манипулятора
            permission.RegisterPermission("darkanticheat.ignore.manipulator", this);
            // игнорирование детекта быстрого хила
            permission.RegisterPermission("darkanticheat.ignore.medicalspeed", this);
            // игнорирование детекта стрельбы в воздухе
            permission.RegisterPermission("darkanticheat.ignore.airshot", this);
            // игнорирование детекта отсутствия урона от падения
            permission.RegisterPermission("darkanticheat.ignore.nofalldamage", this);
            permission.RegisterPermission("darkanticheat.admin", this);

            cmd.AddChatCommand("d.unban", this, CmdUnban);
            cmd.AddChatCommand("dban", this, CmdChatBan);
            cmd.AddConsoleCommand("dunban", this, "CmdConsoleUnban");
            cmd.AddConsoleCommand("dban", this, "CmdConsoleBan");

            foreach (var player in BasePlayer.activePlayerList)
            {
                activePlayers.Add(player);
                lastHealthValues[player.userID] = player.health;
            }
            StartChecking();
            StartStatsResetTimer();
            StartDetectionsResetTimer();
            StartSprintChecking();
            StartSprintViolationsResetTimer();
            StartJumpChecking();
            StartViolationsResetTimer();
            StartJumpResetTimer();
            StartAimOnEokaResetTimer();
            // Удалено StartNoClipViolationsResetTimer
            StartManipulatorViolationsResetTimer();
            StartMedicalSpeedViolationsResetTimer();
            StartAirShotViolationsResetTimer();
            StartHealthMonitoring();
        }

        private void Unload()
        {
            // Останавливаем все таймеры
            StopChecking();
            statsResetTimer?.Destroy();
            detectionsResetTimer?.Destroy();
            sprintCheckTimer?.Destroy();
            sprintViolationsResetTimer?.Destroy();
            jumpViolationsResetTimer?.Destroy();
            jumpResetTimer?.Destroy();
            // noClipViolationsResetTimer?.Destroy(); - удалено
            manipulatorViolationsResetTimer?.Destroy();
            medicalSpeedViolationsResetTimer?.Destroy();
            airShotViolationsResetTimer?.Destroy();

            // Очищаем все словари
            activePlayers.Clear();
            mountTime.Clear();
            banTimers.Clear();
            totalHits.Clear();
            headHits.Clear();
            hitboxViolations.Clear();
            totalShots.Clear();
            headShots.Clear();
            sprintViolations.Clear();
            lastPositions.Clear();
            lastCheckTimes.Clear();
            lastSpeeds.Clear();
            lastAngles.Clear();
            jumpStates.Clear();
            lastTransportExitTime.Clear();
            waterWalkTime.Clear();
            // Удалены строки очистки данных ноуклипа
            // noClipTime.Clear();
            // lastNoClipPositions.Clear();
            // lastJumpTime.Clear();
            // noClipViolations.Clear();
            manipulatorViolations.Clear();
            medicalStartTimes.Clear();
            medicalSpeedViolations.Clear();
            airShotViolations.Clear();
            airHoverStartTime.Clear();
            lastAirPosition.Clear();
            airHoverViolations.Clear();
            lastHealthValues.Clear();
            lastExternalHealTime.Clear();

            // Уничтожаем все таймеры
            foreach (var timer in banTimers.Values)
            {
                timer?.Destroy();
            }

            // Сохраняем конфигурацию
            SaveConfig();
        }

        private void StartChecking()
        {
            StopChecking();
            checkTimer = timer.Every(0.1f, () =>
            {
                CheckPlayers();
                CheckWaterWalk();
                // CheckNoClip(); - удалено
            });
        }

        private void StopChecking()
        {
            if (checkTimer != null && !checkTimer.Destroyed)
            {
                checkTimer.Destroy();
                checkTimer = null;
            }
        }

        void OnPlayerConnected(BasePlayer player)
        {
            if (player == null) return;

            activePlayers.Add(player);
            lastHealthValues[player.userID] = player.health;

            string displayName = "Unknown";
            ulong userId = 0;

            try
            {
                displayName = player.displayName ?? "Unknown";
                userId = player.userID;
            }
            catch
            {
                return;
            }
        }

        void OnPlayerDisconnected(BasePlayer player, string reason)
        {
            if (player == null) return;

            string displayName = player.displayName ?? "Unknown";
            ulong userId = player.userID;

            activePlayers.Remove(player);

            // Очищаем данные игрока
            ulong playerId = player.userID;

            mountTime.Remove(playerId);
            totalHits.Remove(playerId);
            headHits.Remove(playerId);
            hitboxViolations.Remove(playerId);
            totalShots.Remove(playerId);
            headShots.Remove(playerId);
            sprintViolations.Remove(playerId);
            lastPositions.Remove(playerId);
            lastCheckTimes.Remove(playerId);
            lastSpeeds.Remove(playerId);
            lastAngles.Remove(playerId);
            jumpStates.Remove(playerId);
            lastTransportExitTime.Remove(playerId);
            waterWalkTime.Remove(playerId);
            manipulatorViolations.Remove(playerId);
            medicalStartTimes.Remove(playerId);
            medicalSpeedViolations.Remove(playerId);
            airShotViolations.Remove(playerId);
            airHoverStartTime.Remove(playerId);
            lastAirPosition.Remove(playerId);
            airHoverViolations.Remove(playerId);
            lastHealthValues.Remove(playerId);
            lastExternalHealTime.Remove(playerId);

            // Удаляем таймер бана, если он существует
            if (banTimers.ContainsKey(playerId))
            {
                banTimers[playerId]?.Destroy();
                banTimers.Remove(playerId);
            }

            aimOnEokaHitsPerTarget.Remove(userId.ToString());
            aimOnEokaHeadshotsPerTarget.Remove(userId.ToString());
        }

        private bool IsPlayerMinicopterDriver(BasePlayer player)
        {
            if (player == null) return false;

            var mounted = player.GetMounted();
            if (mounted == null) return false;

            if (mounted.ShortPrefabName != "miniheliseat") return false;

            var parent = mounted.GetParentEntity();
            if (parent == null) return false;

            if (parent.ShortPrefabName != "minicopter.entity") return false;

            var vehicle = parent.GetComponent<BaseVehicle>();
            if (vehicle == null || vehicle.mountPoints == null || vehicle.mountPoints.Count == 0) return false;

            var driverSeat = vehicle.mountPoints.FirstOrDefault();
            if (driverSeat?.mountable == null) return false;

            return driverSeat.mountable.GetMounted() == player;
        }

        private void SendDiscordMessage(BasePlayer player, DetectionConfig detection, float actualDistance = 0f, float fakeDistance = 0f, int headHits = 0)
        {
            if (detection == null || !detection.SendDiscordNotification || string.IsNullOrEmpty(detection.WebhookUrl)) return;

            try
            {
                string playerName = "Unknown";
                string playerID = "Unknown";
                string playerURL = "#";

                if (player != null)
                {
                    playerName = player.displayName ?? "Unknown";
                    playerID = player.UserIDString ?? "Unknown";
                    playerURL = $"https://steamcommunity.com/profiles/{player.userID}";
                }

                var match = Regex.Match(detection.BanReason ?? "Unknown", @"\((\d+)\)");
                string detectNumber = match.Success ? $" ({match.Groups[1].Value})" : "";

                string description;
                if (detection is AimOnEokaConfig)
                {
                    if (headHits >= config.AimOnEokaDetection.HeadshotsForDetect)
                        description = $"Моментальный выстрел с Eoka{detectNumber}\n" +
                                     $"Дистанция: {actualDistance:F1}м\n" +
                                     $"Попаданий в голову: {headHits:F0}";
                    else
                        description = $"Моментальный выстрел с Eoka{detectNumber}\n" +
                                     $"Дистанция: {actualDistance:F1}м\n" +
                                     $"Попаданий: {fakeDistance:F0}";
                }
                else if (detection is MinicopterConfig)
                    description = $"Оружие за рулём миникоптера{detectNumber}";
                else if (detection is LongRangeConfig)
                    description = $"Фейк дистанция{detectNumber}\nФейк: {fakeDistance:F1}м\nРеальная: {actualDistance:F1}m";
                else if (detection is SprintingConfig)
                    description = $"Быстрый бег во все стороны{detectNumber}";
                else if (detection is JumpDetectionConfig)
                    description = $"Высокий прыжок{detectNumber}";
                else if (detection is NailgunConfig)
                    description = $"Аим на гвоздемёт{detectNumber}\nДистанция: {actualDistance:F1}м";
                else if (detection is WaterWalkConfig)
                    description = $"Ходьба по воде{detectNumber}";
                else if (detection is ManipulatorConfig)
                    description = $"Манипулятор{detectNumber}";
                else if (detection is MedicalSpeedConfig)
                    description = $"Быстрый хил{detectNumber}";
                else if (detection is AirShotConfig)
                    description = $"Стрельба в воздухе{detectNumber}";
                else
                    description = $"Хитбокс онли голова{detectNumber}";

                var payload = new Dictionary<string, object>
                {
                    ["embeds"] = new[]
                    {
                        new Dictionary<string, object>
                        {
                            ["title"] = "DarkAntiCheat - Обнаружен пидорас",
                            ["color"] = 15158332,
                            ["description"] = description,
                            ["fields"] = new[]
                            {
                                new Dictionary<string, object>
                                {
                                    ["name"] = "Игрок",
                                    ["value"] = $"[{playerName}]({playerURL}) ({playerID})",
                                    ["inline"] = false
                                }
                            },
                            ["timestamp"] = DateTime.UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ")
                        }
                    }
                };

                webrequest.Enqueue(detection.WebhookUrl, JsonConvert.SerializeObject(payload), (code, response) =>
                {
                    if (code != 200 && code != 204 && code == 404)
                    {
                        detection.SendDiscordNotification = false;
                        SaveConfig();
                    }
                }, this, RequestMethod.POST, new Dictionary<string, string> { ["Content-Type"] = "application/json" });
            }
            catch
            {
                detection.SendDiscordNotification = false;
                SaveConfig();
            }
        }

        private void BanPlayer(BasePlayer player, DetectionConfig detection, float actualDistance = 0f, float fakeDistance = 0f)
        {
            if (detection == null || !detection.Enabled) return;
            if (player == null) return;

            // Проверяем, не является ли игрок администратором, чтобы избежать случайного бана
            if (permission.UserHasPermission(player.UserIDString, "darkanticheat.admin"))
            {
                // Лог отключён по запросу
                return;
            }

            string displayName = player.displayName ?? "Unknown";
            ulong userId = player.userID;

            if (banTimers.ContainsKey(userId)) return;

            float banDelay = detection is MinicopterConfig ? (detection as MinicopterConfig).BanDelay : 0f;

            var banTimer = timer.Once(banDelay, () =>
            {
                try
                {
                    if (player != null && player.IsConnected)
                    {
                        bool shouldBan = false;

                        if (detection is MinicopterConfig)
                        {
                            if (IsPlayerMinicopterDriver(player))
                            {
                                var heldEntity = player.GetHeldEntity();
                                var activeItem = player.GetActiveItem();
                                var projectile = heldEntity as BaseProjectile;

                                if (projectile != null || (activeItem != null && IsWeapon(activeItem.info.shortname)))
                                {
                                    shouldBan = true;
                                }
                            }
                        }
                        else
                        {
                            shouldBan = true;
                        }

                        if (shouldBan)
                        {
                            string steamId = player.UserIDString;
                            if (!string.IsNullOrEmpty(steamId))
                            {
                                string ipAddress = player.net?.connection?.ipaddress ?? null;
                                string banCommand;
                                if (!string.IsNullOrEmpty(ipAddress))
                                {
                                    banCommand = $"banid {steamId} {ipAddress} \"{detection.BanReason}\" {detection.BanHours}";
                                }
                                else
                                {
                                    // Если IP недоступен (например, игрок отключился), баним только по SteamID
                                    banCommand = $"banid {steamId} \"{detection.BanReason}\" {detection.BanHours}";
                                }
                                Server.Command(banCommand);

                                if (detection.SendDiscordNotification)
                                {
                                    SendDiscordMessage(player, detection, actualDistance, fakeDistance);
                                }
                            }
                        }
                    }
                }
                catch
                {
                    return;
                }
                finally
                {
                    banTimers.Remove(userId);
                }
            });

            banTimers[userId] = banTimer;
        }

        private bool IsWeapon(string itemName)
        {
            if (string.IsNullOrEmpty(itemName)) return false;

            itemName = itemName.ToLower();
            return itemName.Contains("rifle") ||
                   itemName.Contains("pistol") ||
                   itemName.Contains("smg") ||
                   itemName.Contains("bow") ||
                   itemName.Contains("shotgun") ||
                   itemName.Contains("launcher") ||
                   itemName.Contains("gun") ||
                   itemName.Contains("ak47") ||
                   itemName.Contains("lr300") ||
                   itemName.Contains("mp5") ||
                   itemName.Contains("thompson") ||
                   itemName.Contains("m39") ||
                   itemName.Contains("m92") ||
                   itemName.Contains("m249") ||
                   itemName.Contains("l96") ||
                   itemName.Contains("bolt") ||
                   itemName.Contains("python") ||
                   itemName.Contains("revolver") ||
                   itemName.Contains("semi");
        }

        void OnWeaponFired(BaseProjectile projectile, BasePlayer player, ItemModProjectile mod, ProtoBuf.ProjectileShoot projectileShoot)
        {
            if (player == null || !player.IsConnected || projectile == null || projectileShoot == null) return;
            if (permission.UserHasPermission(player.UserIDString, "darkanticheat.ignore.all")) return;

            // Детект стрельбы в воздухе
            if (config.AirShotDetection.Enabled && !permission.UserHasPermission(player.UserIDString, "darkanticheat.ignore.airshot"))
            {
                // Проверяем тип оружия
                var weapon = player.GetActiveItem();
                if (weapon != null && (weapon.info.shortname.Contains("bow") || weapon.info.shortname.Contains("compound")))
                {
                    return;
                }

                // Проверяем, что игрок не на земле и не на лестнице
                if (!player.IsOnGround() && !player.OnLadder())
                {
                    // Проверяем высоту над землей
                    var rayStart = player.transform.position;
                    var rayEnd = rayStart + Vector3.down * 10f;
                    RaycastHit hit;

                    if (Physics.Raycast(rayStart, Vector3.down, out hit, 10f))
                    {
                        float heightAboveGround = hit.distance;

                        // Если высота больше минимальной для детекта
                        if (heightAboveGround >= config.AirShotDetection.MinHeight)
                        {
                            // Проверяем дистанцию до ближайшего игрока
                            float closestDistance = float.MaxValue;
                            foreach (var target in BasePlayer.activePlayerList)
                            {
                                if (target == player) continue;
                                float distance = Vector3.Distance(player.transform.position, target.transform.position);
                                if (distance < closestDistance)
                                    closestDistance = distance;
                            }

                            if (closestDistance >= config.AirShotDetection.MinDistance)
                            {
                                if (!airShotViolations.ContainsKey(player.userID))
                                    airShotViolations[player.userID] = 0;

                                airShotViolations[player.userID]++;

                                if (airShotViolations[player.userID] >= config.AirShotDetection.DetectsForBan)
                                {
                                    Server.Command($"banid {player.UserIDString} {config.AirShotDetection.BanHours} \"{config.AirShotDetection.BanReason}\"");
                                    if (config.AirShotDetection.SendDiscordNotification)
                                    {
                                        SendDiscordMessage(player, config.AirShotDetection, closestDistance, heightAboveGround);
                                    }
                                    airShotViolations.Remove(player.userID);
                                    return;
                                }
                            }
                        }
                    }
                }
            }

            // Проверка манипулятора
            if (config.ManipulatorDetection.Enabled && !permission.UserHasPermission(player.UserIDString, "darkanticheat.ignore.manipulator"))
            {
                // Добавляем отладочный лог начала проверки манипулятора
                // Puts($"[DEBUG] Проверка манипулятора для игрока {player.displayName} ({player.UserIDString})");
                
                // Проверяем все снаряды в выстреле
                foreach (var projectileEntry in projectileShoot.projectiles)
                {
                    // Получаем информацию о снаряде
                    Vector3 projectileVelocity = projectileEntry.startVel;
                    Vector3 projectilePosition = projectileEntry.startPos;
                    Vector3 playerEyePosition = player.eyes.position;
                    Vector3 playerEyeForward = player.eyes.BodyForward();
                    
                    // Вычисляем расстояние между позицией снаряда и позицией глаз игрока
                    float distanceOffset = Vector3.Distance(projectilePosition, playerEyePosition);
                    
                    // Проверяем, находится ли расстояние в пределах детекта
                    if (distanceOffset >= config.ManipulatorDetection.MinBulletOffset && 
                        distanceOffset <= config.ManipulatorDetection.MaxBulletOffset)
                    {
                        // Проверяем угол между направлением взгляда и направлением снаряда
                        Vector3 projectileDirection = projectileVelocity.normalized;
                        float angleOffset = Vector3.Angle(playerEyeForward, projectileDirection);
                        
                        // Если угол слишком большой, это может быть манипулятор
                        if (angleOffset > 10f)
                        {
                            // Фиксируем подозрительный выстрел, но не считаем нарушение до фактического хита по игроку
                            manipulatorSuspicionTime[player.userID] = UnityEngine.Time.realtimeSinceStartup;
                        }
                    }
                }
            }
        }

        void OnPlayerAttack(BasePlayer attacker, HitInfo hitInfo)
        {
            if (attacker == null || !attacker.IsConnected || hitInfo == null) return;
            if (permission.UserHasPermission(attacker.UserIDString, "darkanticheat.ignore.all")) return;

            // Остальной код OnPlayerAttack...
            var victim = hitInfo.HitEntity as BasePlayer;

            // Модифицированное условие: проверяем только что это игрок и не сам атакующий
            if (victim != null && victim.IsConnected && attacker != victim)
            {
                // Считаем все попадания по игрокам
                if (!totalShots.ContainsKey(attacker.userID))
                {
                    totalShots[attacker.userID] = 0;
                    headShots[attacker.userID] = 0;
                }

                totalShots[attacker.userID]++;
                if (hitInfo.isHeadshot)
                {
                    headShots[attacker.userID]++;
                }

                // Проверка хитбоксов (для всех подключенных игроков)
                if (config.HitboxDetection.Enabled && hitInfo.isHeadshot)
                {
                    var targetPlayer = hitInfo.HitEntity as BasePlayer;
                    if (targetPlayer != null)
                    {
                        Vector3 hitPoint = hitInfo.HitPositionWorld;
                        Vector3 pointStart = hitInfo.PointStart;
                        Vector3 pointEnd = hitInfo.PointEnd;
                        Vector3 headPosition = targetPlayer.eyes.position;
                        float distanceToHead = Vector3.Distance(hitPoint, headPosition);

                        // Расчет расстояния между игроками
                        float playerDistance = Vector3.Distance(attacker.transform.position, targetPlayer.transform.position);

                        // Проверяем расстояние до головы и хедшот
                        if (distanceToHead > 0.1f && hitInfo.isHeadshot)
                        {
                            // Проверяем дистанцию между игроками (детектим только если дальше минимальной дистанции)
                            if (playerDistance > config.HitboxDetection.MinDetectDistance)
                            {
                                // Проверяем процент попаданий в голову (только если сделано минимум 3 выстрела)
                                if (totalShots[attacker.userID] >= 3)
                                {
                                    float headshotPercentage = (float)headShots[attacker.userID] / totalShots[attacker.userID] * 100f;

                                    if (headshotPercentage >= config.HitboxDetection.HeadshotPercentage)
                                    {
                                        if (!hitboxViolations.ContainsKey(attacker.userID))
                                        {
                                            hitboxViolations[attacker.userID] = 1;
                                        }
                                        else
                                        {
                                            hitboxViolations[attacker.userID]++;
                                        }

                                        if (hitboxViolations[attacker.userID] >= config.HitboxDetection.DetectsForBan)
                                        {
                                            Server.Command($"banid {attacker.UserIDString} {config.HitboxDetection.BanHours} \"{config.HitboxDetection.BanReason}\"");
                                            SendDiscordMessage(attacker, config.HitboxDetection);
                                            hitboxViolations.Remove(attacker.userID);
                                            totalShots.Remove(attacker.userID);
                                            headShots.Remove(attacker.userID);
                                            return;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Новая логика для детекта "Аим на Еоку"
            if (config.AimOnEokaDetection != null && config.AimOnEokaDetection.Enabled)
            {
                var weapon = attacker.GetActiveItem()?.info?.shortname;
                if (weapon == "pistol.eoka")
                {
                    // Проверяем, что попадание было только по игроку
                    var targetPlayer = hitInfo.HitEntity as BasePlayer;

                    // Пропускаем, если цель не игрок или это сам стрелок
                    if (targetPlayer == null || targetPlayer == attacker) return;

                    Vector3 attackerPos = attacker.transform.position;
                    Vector3 victimPos = targetPlayer.transform.position;
                    float distance = Vector3.Distance(attackerPos, victimPos);
                    float projectileDistance = hitInfo.ProjectileDistance;
                    float actualDistance = Mathf.Max(distance, projectileDistance);

                    if (actualDistance >= config.AimOnEokaDetection.MinDistance)
                    {
                        string key = $"{attacker.userID}_{targetPlayer.userID}";

                        // Инициализация словарей для конкретной пары атакующий-цель
                        if (!aimOnEokaHitsPerTarget.ContainsKey(key))
                            aimOnEokaHitsPerTarget[key] = new Dictionary<ulong, int>();
                        if (!aimOnEokaHeadshotsPerTarget.ContainsKey(key))
                            aimOnEokaHeadshotsPerTarget[key] = new Dictionary<ulong, int>();

                        // Увеличиваем счетчик общих попаданий для конкретной цели
                        if (!aimOnEokaHitsPerTarget[key].ContainsKey(attacker.userID))
                            aimOnEokaHitsPerTarget[key][attacker.userID] = 0;
                        aimOnEokaHitsPerTarget[key][attacker.userID]++;

                        if (hitInfo.isHeadshot)
                        {
                            if (!aimOnEokaHeadshotsPerTarget[key].ContainsKey(attacker.userID))
                                aimOnEokaHeadshotsPerTarget[key][attacker.userID] = 0;
                            aimOnEokaHeadshotsPerTarget[key][attacker.userID]++;
                        }

                        bool shouldDetect = false;
                        bool isHeadshotDetect = false;

                        // Проверяем условия детекта для конкретной цели
                        if (aimOnEokaHeadshotsPerTarget.ContainsKey(key) &&
                            aimOnEokaHeadshotsPerTarget[key].ContainsKey(attacker.userID) &&
                            aimOnEokaHeadshotsPerTarget[key][attacker.userID] >= config.AimOnEokaDetection.HeadshotsForDetect)
                        {
                            shouldDetect = true;
                            isHeadshotDetect = true;
                        }
                        else if (aimOnEokaHitsPerTarget.ContainsKey(key) &&
                                 aimOnEokaHitsPerTarget[key].ContainsKey(attacker.userID) &&
                                 aimOnEokaHitsPerTarget[key][attacker.userID] >= config.AimOnEokaDetection.TotalHitsForDetect)
                        {
                            shouldDetect = true;
                            isHeadshotDetect = false;
                        }

                        if (shouldDetect)
                        {
                            float shotDistance = Vector3.Distance(attackerPos, victimPos);
                            // Сохраняем значения перед очисткой
                            int totalHits = 0;
                            int totalHeadshots = 0;

                            // Безопасное получение значений
                            if (aimOnEokaHitsPerTarget.ContainsKey(key) &&
                                aimOnEokaHitsPerTarget[key].ContainsKey(attacker.userID))
                            {
                                totalHits = aimOnEokaHitsPerTarget[key][attacker.userID];
                            }

                            if (aimOnEokaHeadshotsPerTarget.ContainsKey(key) &&
                                aimOnEokaHeadshotsPerTarget[key].ContainsKey(attacker.userID))
                            {
                                totalHeadshots = aimOnEokaHeadshotsPerTarget[key][attacker.userID];
                            }

                            // Очищаем статистику до бана
                            aimOnEokaHitsPerTarget.Remove(key);
                            aimOnEokaHeadshotsPerTarget.Remove(key);

                            // Отправляем вебхук с нужным форматом
                            if (config.AimOnEokaDetection.SendDiscordNotification)
                            {
                                if (isHeadshotDetect)
                                {
                                    SendDiscordMessage(attacker, config.AimOnEokaDetection,
                                        shotDistance,
                                        0,
                                        totalHeadshots);
                                }
                                else
                                {
                                    SendDiscordMessage(attacker, config.AimOnEokaDetection,
                                        shotDistance,
                                        totalHits,
                                        0);
                                }
                            }

                            // Баним после отправки вебхука
                            Server.Command($"banid {attacker.UserIDString} {config.AimOnEokaDetection.BanHours} \"{config.AimOnEokaDetection.BanReason}\"");
                        }
                    }
                }
            }

            // Завершаем детект манипулятора: проверяем, был ли недавний подозрительный выстрел и сейчас пуля попала по игроку
            if (config.ManipulatorDetection.Enabled && !permission.UserHasPermission(attacker.UserIDString, "darkanticheat.ignore.manipulator"))
            {
                const float suspicionWindow = 0.6f; // секунды
                float currentTime = UnityEngine.Time.realtimeSinceStartup;

                if (manipulatorSuspicionTime.ContainsKey(attacker.userID) &&
                    currentTime - manipulatorSuspicionTime[attacker.userID] <= suspicionWindow)
                {
                    manipulatorSuspicionTime.Remove(attacker.userID);

                    if (!manipulatorViolations.ContainsKey(attacker.userID))
                        manipulatorViolations[attacker.userID] = 0;

                    manipulatorViolations[attacker.userID]++;

                    if (manipulatorViolations[attacker.userID] >= config.ManipulatorDetection.DetectsForBan)
                    {
                        Server.Command($"banid {attacker.UserIDString} {config.ManipulatorDetection.BanHours} \"{config.ManipulatorDetection.BanReason}\"");

                        if (config.ManipulatorDetection.SendDiscordNotification)
                        {
                            SendDiscordMessage(attacker, config.ManipulatorDetection);
                        }

                        manipulatorViolations.Remove(attacker.userID);
                    }
                }
            }
        }

        void OnPlayerDeath(BasePlayer victim, HitInfo hitInfo)
        {
            if (victim == null || hitInfo == null || hitInfo.Initiator == null) return;

            var attacker = hitInfo.Initiator as BasePlayer;
            if (attacker == null || !attacker.IsConnected) return;
            if (permission.UserHasPermission(attacker.UserIDString, "darkanticheat.ignore.all")) return;
            if (attacker == victim) return;

            // Проверяем, было ли убийство совершено оружием
            var weapon = hitInfo.Weapon;
            if (weapon == null) return;

            // Получаем shortname оружия
            var weaponName = weapon.ShortPrefabName;
            if (string.IsNullOrEmpty(weaponName)) return;

            // Проверяем, является ли это оружием
            if (!IsWeapon(weaponName)) return;

            // Проверяем, является ли оружие проектильным
            var projectile = weapon as BaseProjectile;
            if (projectile == null) return;

            if (IsPlayerMinicopterDriver(attacker) && !permission.UserHasPermission(attacker.UserIDString, "darkanticheat.ignore.minicopter"))
            {
                BanPlayer(attacker, config.MinicopterDetection, 0f, 0f);
                return;
            }

            float actualDistance = Vector3.Distance(hitInfo.PointStart, hitInfo.HitPositionWorld);
            float projectileDistance = hitInfo.ProjectileDistance;

            // Детект гвоздемёта
            if ((weaponName == "nailgun" || weaponName == "nailgun.entity" || weaponName == "nailgun_prefab") && !permission.UserHasPermission(attacker.UserIDString, "darkanticheat.ignore.nailgun"))
            {
                float effectiveDistance = Mathf.Max(actualDistance, projectileDistance);
                if (config.NailgunDetection.Enabled && effectiveDistance >= config.NailgunDetection.MinDistance)
                {
                    BanPlayer(attacker, config.NailgunDetection, effectiveDistance, 0f);
                    return;
                }
            }

            // Проверяем превышение минимальной дистанции
            if (!permission.UserHasPermission(attacker.UserIDString, "darkanticheat.ignore.longrange"))
            {
                if (actualDistance >= config.LongRangeDetection.MinDistance || projectileDistance >= config.LongRangeDetection.MinDistance)
                {
                    float fakeDistance = Mathf.Max(projectileDistance, actualDistance);
                    float distanceDifference = Mathf.Abs(actualDistance - projectileDistance);

                    if (distanceDifference < 0.1f) return;

                    BanPlayer(attacker, config.LongRangeDetection, actualDistance, fakeDistance);
                }
            }
        }

        void OnEntityMounted(BaseVehicle vehicle, BasePlayer player)
        {
            if (player == null) return;
            mountTime[player.userID] = UnityEngine.Time.realtimeSinceStartup;
        }

        void OnEntityDismounted(BaseVehicle vehicle, BasePlayer player)
        {
            if (player == null) return;
            mountTime.Remove(player.userID);
        }

        private void CheckPlayers()
        {
            foreach (var player in activePlayers.ToList())
            {
                if (player == null || !player.IsConnected) continue;
                if (permission.UserHasPermission(player.UserIDString, "darkanticheat.ignore.all")) continue;

                if (IsPlayerMinicopterDriver(player))
                {
                    var heldEntity = player.GetHeldEntity();
                    var activeItem = player.GetActiveItem();

                    var projectile = heldEntity as BaseProjectile;
                    if (projectile != null || (activeItem != null && IsWeapon(activeItem.info.shortname)))
                    {
                        BanPlayer(player, config.MinicopterDetection, 0f, 0f);
                        continue;
                    }
                }
            }
        }

        private void StartStatsResetTimer()
        {
            statsResetTimer?.Destroy();
            statsResetTimer = timer.Every(config.HitboxDetection.StatsResetInterval, () =>
            {
                totalShots.Clear();
                headShots.Clear();
            });
        }

        private void StartDetectionsResetTimer()
        {
            detectionsResetTimer?.Destroy();
            detectionsResetTimer = timer.Every(config.HitboxDetection.DetectionsResetInterval, () =>
            {
                hitboxViolations.Clear();
            });
        }

        private void StartSprintChecking()
        {
            sprintCheckTimer?.Destroy();
            sprintCheckTimer = timer.Every(0.25f, CheckSprintViolations);
        }

        private void CheckSprintViolations()
        {
            if (!config.SprintingDetection.Enabled) return;

            foreach (var player in activePlayers.ToList())
            {
                if (player == null || !player.IsConnected) continue;
                if (permission.UserHasPermission(player.UserIDString, "darkanticheat.ignore.all")) continue;
                if (permission.UserHasPermission(player.UserIDString, "darkanticheat.ignore.sprint")) continue;
                if (player.IsSleeping() || player.IsDead() || player.IsWounded()) continue;
                if (!player.IsOnGround() || player.IsSwimming()) continue;

                var currentTime = UnityEngine.Time.realtimeSinceStartup;
                var currentPos = player.transform.position;

                // Если активен период игнорирования детекта спринта, пропускаем проверки
                float ignoreUntil;
                if (sprintIgnoreUntil.TryGetValue(player.userID, out ignoreUntil))
                {
                    if (currentTime < ignoreUntil)
                    {
                        lastPositions[player.userID] = player.transform.position;
                        lastCheckTimes[player.userID] = currentTime;
                        continue;
                    }
                    else
                    {
                        // Время игнорирования истекло — удаляем запись
                        sprintIgnoreUntil.Remove(player.userID);
                    }
                }

                // Проверяем наличие сущностей в радиусе 3 метров
                var nearbyEntities = Physics.OverlapSphere(currentPos, 3f);
                bool hasNearbyEntities = false;

                foreach (var collider in nearbyEntities)
                {
                    var entity = collider.GetComponent<BaseEntity>();
                    if (entity != null)
                    {
                        // Проверяем, является ли сущность игроком или NPC
                        if ((entity is BasePlayer && entity != player) || entity is NPCPlayer || entity is BaseNpc)
                        {
                            hasNearbyEntities = true;
                            break;
                        }
                    }
                }

                // Если есть сущности рядом, пропускаем проверки
                if (hasNearbyEntities)
                {
                    continue;
                }

                // Проверяем, находится ли игрок в транспорте
                bool isInTransport = player.isMounted || player.GetMounted() != null ||
                                    IsPlayerOnCargo(player) || IsPlayerOnSubwayOrTrain(player);

                // Если игрок в транспорте, обновляем время и пропускаем проверку
                if (isInTransport)
                {
                    lastTransportExitTime[player.userID] = currentTime;
                    continue;
                }

                // Проверяем безопасный период после выхода из транспорта (6 секунд)
                if (lastTransportExitTime.ContainsKey(player.userID) &&
                    currentTime - lastTransportExitTime[player.userID] < 6f)
                {
                    continue;
                }

                // Проверяем, не стоит ли игрок на движущемся транспорте
                var rayStart = player.transform.position;
                var rayEnd = rayStart + Vector3.down * 3f;
                var hits = Physics.RaycastAll(rayStart, Vector3.down, 3f);
                bool isStandingOnTransport = false;

                foreach (var hit in hits)
                {
                    var entity = RaycastHitEx.GetEntity(hit);
                    if (entity != null)
                    {
                        // Проверяем является ли объект транспортом или частью метро
                        if (entity is BaseVehicle || entity is BaseVehicleModule ||
                            (entity.PrefabName != null && (entity.PrefabName.Contains("/subentity") || entity.PrefabName.Contains("/traincar"))))
                        {
                            isStandingOnTransport = true;
                            lastTransportExitTime[player.userID] = currentTime;
                            break;
                        }
                    }
                }

                if (isStandingOnTransport)
                {
                    continue;
                }

                if (!lastPositions.ContainsKey(player.userID))
                {
                    lastPositions[player.userID] = currentPos;
                    lastCheckTimes[player.userID] = currentTime;
                    continue;
                }

                var timeDiff = currentTime - lastCheckTimes[player.userID];
                if (timeDiff < config.SprintingDetection.CheckInterval) continue;

                var distance = Vector3.Distance(currentPos, lastPositions[player.userID]);
                var speed = distance / timeDiff;

                // Если скорость превышает порог более чем на 12 единиц, игнорируем детекты на 6 секунд
                if (speed > config.SprintingDetection.MaxSpeedThreshold + 12f)
                {
                    sprintIgnoreUntil[player.userID] = currentTime + 6f;
                    lastPositions[player.userID] = currentPos;
                    lastCheckTimes[player.userID] = currentTime;
                    continue;
                }

                // Проверяем вертикальное движение
                float verticalDistance = Mathf.Abs(currentPos.y - lastPositions[player.userID].y);
                if (verticalDistance > 0.2f)
                {
                    lastPositions[player.userID] = currentPos;
                    lastCheckTimes[player.userID] = currentTime;
                    continue;
                }

                // Проверяем скорость движения
                if (speed > 5.0f)
                {
                    Vector3 moveDirection = (currentPos - lastPositions[player.userID]).normalized;
                    Vector3 playerForward = player.eyes.rotation * Vector3.forward;
                    Vector3 playerRight = player.eyes.rotation * Vector3.right;

                    moveDirection.y = 0;
                    playerForward.y = 0;
                    playerRight.y = 0;

                    if (moveDirection.magnitude > 0.001f) moveDirection.Normalize();
                    if (playerForward.magnitude > 0.001f) playerForward.Normalize();
                    if (playerRight.magnitude > 0.001f) playerRight.Normalize();

                    float forwardDot = Vector3.Dot(moveDirection, playerForward);
                    float rightDot = Mathf.Abs(Vector3.Dot(moveDirection, playerRight));
                    float angle = Vector3.Angle(moveDirection, playerForward);

                    // Проверяем угол поворота
                    if (!lastAngles.ContainsKey(player.userID))
                    {
                        lastAngles[player.userID] = player.eyes.rotation.eulerAngles;
                        lastPositions[player.userID] = currentPos;
                        lastCheckTimes[player.userID] = currentTime;
                        continue;
                    }

                    float currentYaw = player.eyes.rotation.eulerAngles.y;
                    float lastYaw = lastAngles[player.userID].y;
                    float yawDelta = Mathf.Abs(Mathf.DeltaAngle(currentYaw, lastYaw));

                    // Обновляем значения для следующей проверки
                    lastAngles[player.userID] = player.eyes.rotation.eulerAngles;
                    lastPositions[player.userID] = currentPos;
                    lastCheckTimes[player.userID] = currentTime;

                    // Если игрок быстро двигает мышкой, пропускаем проверку
                    if (yawDelta > 2.5f)
                    {
                        lastPositions[player.userID] = currentPos;
                        lastCheckTimes[player.userID] = currentTime;
                        continue;
                    }

                    // Проверяем движение в сторону/назад с учетом скорости
                    bool isStrafing = rightDot > 0.6f;
                    bool isMovingBackward = forwardDot < -0.5f;

                    float maxStrafeSpeed = config.SprintingDetection.MaxSpeedThreshold;
                    float maxBackwardSpeed = config.SprintingDetection.MaxSpeedThreshold;
                    float maxAngleSpeed = config.SprintingDetection.MaxSpeedThreshold;

                    if ((isStrafing && speed > maxStrafeSpeed) ||
                        (isMovingBackward && speed > maxBackwardSpeed) ||
                        (angle > 30f && speed > maxAngleSpeed && yawDelta < 1.5f))
                    {
                        if (!sprintViolations.ContainsKey(player.userID))
                            sprintViolations[player.userID] = 0;

                        sprintViolations[player.userID]++;

                        if (sprintViolations[player.userID] >= config.SprintingDetection.DetectsForBan)
                        {
                            sprintViolations.Remove(player.userID);
                            timer.Once(0.1f, () =>
                            {
                                if (player != null && player.IsConnected)
                                {
                                    Server.Command($"banid {player.UserIDString} {config.SprintingDetection.BanHours} \"{config.SprintingDetection.BanReason}\"");
                                    timer.Once(0.1f, () =>
                                    {
                                        if (config.SprintingDetection.SendDiscordNotification)
                                        {
                                            SendDiscordMessage(player, config.SprintingDetection);
                                        }
                                    });
                                }
                            });
                        }
                    }
                }

                lastPositions[player.userID] = currentPos;
                lastCheckTimes[player.userID] = currentTime;
            }
        }

        private void StartSprintViolationsResetTimer()
        {
            sprintViolationsResetTimer?.Destroy();
            sprintViolationsResetTimer = timer.Every(config.SprintingDetection.ViolationResetTime, () =>
            {
                sprintViolations.Clear();
            });
        }

        private void StartJumpChecking()
        {
            timer.Every(0.02f, () =>
            {
                foreach (var player in activePlayers.ToList())
                {
                    if (player == null || !player.IsConnected) continue;
                    if (permission.UserHasPermission(player.UserIDString, "darkanticheat.ignore.all")) continue;
                    if (permission.UserHasPermission(player.UserIDString, "darkanticheat.ignore.jump")) continue;
                    if (player.IsSleeping() || player.IsDead() || player.IsWounded()) continue;
                    if (player.IsSwimming()) continue;

                    var currentTime = UnityEngine.Time.realtimeSinceStartup;
                    var currentPos = player.transform.position;
                    var isOnGround = player.IsOnGround();
                    var isOnLadder = player.OnLadder();

                    // Проверяем, находится ли игрок в транспорте
                    bool isInTransport = player.isMounted || player.GetMounted() != null || IsPlayerOnCargo(player);

                    // Проверяем, стоит ли игрок на транспорте
                    bool isStandingOnTransport = false;
                    var rayStart = player.transform.position;
                    var hits = Physics.RaycastAll(rayStart, Vector3.down, 1f);
                    foreach (var hit in hits)
                    {
                        var entity = RaycastHitEx.GetEntity(hit);
                        if (entity != null && (entity is BaseVehicle || entity is BaseVehicleModule))
                        {
                            isStandingOnTransport = true;
                            break;
                        }
                    }

                    // Если игрок в транспорте или стоит на нем, обновляем время и пропускаем проверку
                    if (isInTransport || isStandingOnTransport)
                    {
                        lastTransportExitTime[player.userID] = currentTime;
                        continue;
                    }

                    // Проверяем безопасный период после выхода из транспорта (10 секунд)
                    if (lastTransportExitTime.ContainsKey(player.userID) &&
                        currentTime - lastTransportExitTime[player.userID] < 10f)
                    {
                        continue;
                    }

                    if (!jumpStates.ContainsKey(player.userID))
                    {
                        jumpStates[player.userID] = new JumpState
                        {
                            LastGroundY = currentPos.y,
                            MaxJumpHeight = currentPos.y,
                            WasInAir = !isOnGround,
                            LastJumpTime = currentTime,
                            JumpCount = 0,
                            LastLadderTime = isOnLadder ? currentTime : 0f,
                            WasOnLadder = isOnLadder,
                            Violations = 0,
                            LastSpeedCheckTime = currentTime,
                            LastSpeed = 0f,
                            WasRunning = false
                        };
                        continue;
                    }

                    var state = jumpStates[player.userID];

                    // Проверяем лестницу
                    if (isOnLadder)
                    {
                        state.LastLadderTime = currentTime;
                        state.WasOnLadder = true;
                        state.LastGroundY = currentPos.y;
                        state.MaxJumpHeight = currentPos.y;
                        state.WasInAir = false;
                        return;
                    }

                    // Проверяем скорость движения для учета прыжка с разбега
                    if (currentTime - state.LastSpeedCheckTime >= 0.1f)
                    {
                        var velocity = player.GetParentVelocity();
                        state.LastSpeed = velocity.magnitude;
                        state.WasRunning = state.LastSpeed > 5.5f;
                        state.LastSpeedCheckTime = currentTime;
                    }

                    // Обновляем состояние на земле
                    if (isOnGround)
                    {
                        if (!state.WasInAir)
                        {
                            state.LastGroundY = currentPos.y;
                            state.MaxJumpHeight = currentPos.y;
                            state.JumpCount = 0;
                        }
                        else
                        {
                            // Проверка отсутствия урона от падения
                            if (config.NoFallDamageDetection != null && config.NoFallDamageDetection.Enabled &&
                                !permission.UserHasPermission(player.UserIDString, "darkanticheat.ignore.nofalldamage"))
                            {
                                float fallHeight = state.MaxJumpHeight - currentPos.y;
                                float damageTaken = state.StartHealth - player.health;
                                // Puts($"[DEBUG] Проверка падения: высота={fallHeight:F1}м, урон={damageTaken:F1} для {player.displayName}");
                                if (fallHeight >= config.NoFallDamageDetection.MinFallHeight)
                                {
                                    if (damageTaken < 1f)
                                    {
                                        Puts($"[DEBUG] Нет урона от падения у {player.displayName} ({player.UserIDString}) высота {fallHeight:F1}м");
                                    }
                                }
                            }

                            state.WasInAir = false;
                            state.JumpCount = 0;
                        }
                    }
                    else // В воздухе
                    {
                        // Начало прыжка
                        if (!state.WasInAir)
                        {
                            state.LastJumpTime = currentTime;
                            state.MaxJumpHeight = currentPos.y;
                            state.JumpCount++;
                            state.WasInAir = true;
                            state.StartHealth = player.health;
                        }
                        // Во время прыжка
                        else if (currentPos.y > state.MaxJumpHeight)
                        {
                            state.MaxJumpHeight = currentPos.y;

                            float jumpHeight = state.MaxJumpHeight - state.LastGroundY;
                            float timeSinceJump = currentTime - state.LastJumpTime;

                            // Базовый лимит высоты прыжка
                            float maxAllowedHeight = config.JumpDetection.MaxJumpHeight;

                            // Увеличиваем допустимую высоту при беге
                            if (state.WasRunning)
                            {
                                maxAllowedHeight *= 1.1f;
                            }

                            // Проверяем нарушение
                            if (jumpHeight > maxAllowedHeight && timeSinceJump < 0.8f)
                            {
                                state.Violations++;

                                if (state.Violations >= config.JumpDetection.DetectsForBan)
                                {
                                    BanPlayer(player, config.JumpDetection);
                                    jumpStates.Remove(player.userID);
                                    return;
                                }
                            }
                        }
                    }
                }
            });
        }

        private void StartViolationsResetTimer()
        {
            jumpViolationsResetTimer?.Destroy();
            jumpViolationsResetTimer = timer.Every(600f, () =>
            {
                sprintViolations.Clear();
                hitboxViolations.Clear();
            });
        }

        private void StartJumpResetTimer()
        {
            jumpResetTimer?.Destroy();
            jumpResetTimer = timer.Every(120f, () =>
            {
                foreach (var state in jumpStates.Values)
                {
                    state.Violations = 0;
                }
            });
        }

        private void StartAimOnEokaResetTimer()
        {
            timer.Every(300f, () =>
            {
                aimOnEokaHitsPerTarget.Clear();
                aimOnEokaHeadshotsPerTarget.Clear();
            });
        }

        private void StartManipulatorViolationsResetTimer()
        {
            manipulatorViolationsResetTimer?.Destroy();
            manipulatorViolationsResetTimer = timer.Every(180f, () =>
            {
                manipulatorViolations.Clear();
            });
        }

        private void StartMedicalSpeedViolationsResetTimer()
        {
            medicalSpeedViolationsResetTimer?.Destroy();
            medicalSpeedViolationsResetTimer = timer.Every(config.MedicalSpeedDetection.ViolationResetTime, () =>
            {
                medicalSpeedViolations.Clear();
            });
        }

        private void StartAirShotViolationsResetTimer()
        {
            airShotViolationsResetTimer?.Destroy();
            airShotViolationsResetTimer = timer.Every(120f, () =>
            {
                airShotViolations.Clear();
            });
        }

        private void CmdUnban(BasePlayer player, string command, string[] args)
        {
            if (player == null) return;

            string adminName = player.displayName ?? "Unknown";
            ulong adminId = player.userID;

            if (!permission.UserHasPermission(player.UserIDString, "darkanticheat.unban"))
            {
                player.ChatMessage("У вас нет прав на использование этой команды! Требуется право: darkanticheat.unban");
                return;
            }

            if (args.Length != 1)
            {
                player.ChatMessage("Использование: /d.unban <steamid>");
                return;
            }

            string targetId = args[0];
            if (string.IsNullOrEmpty(targetId))
            {
                player.ChatMessage("Укажите корректный SteamID!");
                return;
            }

            Server.Command($"unban {targetId}");
            ConsoleSystem.Run(ConsoleSystem.Option.Server, "writecfg", null);
            player.ChatMessage($"Игрок {targetId} успешно разбанен");
        }

        private void CheckWaterWalk()
        {
            if (!config.WaterWalkDetection.Enabled) return;

            foreach (var player in activePlayers.ToList())
            {
                if (player == null || !player.IsConnected) continue;
                if (permission.UserHasPermission(player.UserIDString, "darkanticheat.ignore.all")) continue;
                if (permission.UserHasPermission(player.UserIDString, "darkanticheat.ignore.waterwalk")) continue;
                if (player.IsSleeping() || player.IsDead() || player.IsWounded()) continue;
                if (player.isMounted || player.GetMounted() != null) continue;

                var currentTime = UnityEngine.Time.realtimeSinceStartup;

                // Проверяем, находится ли игрок в воде
                bool isInWater = WaterLevel.Test(player.transform.position);
                bool isSwimming = player.IsSwimming();
                float waterDepth = WaterLevel.GetOverallWaterDepth(player.transform.position, true, player, true);
                Vector3 currentPos = player.transform.position;
                var waterInfo = WaterLevel.GetWaterInfo(player.transform.position, true, player, true);

                // Проверяем, тонет ли игрок
                bool isDrowning = player.metabolism.wetness.value > 0.5f;

                // Проверяем наличие лодки или обломков под игроком
                bool isAboveBoat = false;
                var hits = Physics.RaycastAll(currentPos, Vector3.down, 3f);
                foreach (var hit in hits)
                {
                    var entity = RaycastHitEx.GetEntity(hit);
                    if (entity != null)
                    {
                        // Проверяем все типы лодок и их обломки
                        if (entity.PrefabName != null && (
                            entity.PrefabName.Contains("/rowboat") ||
                            entity.PrefabName.Contains("/rhib") ||
                            entity.PrefabName.Contains("/boat") ||
                            entity.PrefabName.Contains("/debris")))
                        {
                            isAboveBoat = true;
                            break;
                        }
                    }
                }

                // Если игрок в глубокой воде, не плавает и не тонет
                if (isInWater && !isSwimming && waterDepth > 1.5f && !isDrowning && !isAboveBoat)
                {
                    if (!waterWalkTime.ContainsKey(player.userID))
                    {
                        waterWalkTime[player.userID] = currentTime;
                    }
                    else
                    {
                        float timeOnWater = currentTime - waterWalkTime[player.userID];

                        if (timeOnWater >= config.WaterWalkDetection.MinDetectTime)
                        {
                            BanPlayer(player, config.WaterWalkDetection);
                            waterWalkTime.Remove(player.userID);
                        }
                    }
                }
                else if (waterWalkTime.ContainsKey(player.userID))
                {
                    waterWalkTime.Remove(player.userID);
                }
            }
        }

        private bool IsPlayerOnCargo(BasePlayer player)
        {
            if (player == null) return false;
            var parent = player.GetParentEntity();
            if (parent == null) return false;
            var prefabName = parent.PrefabName;
            return prefabName != null && prefabName.Contains("/cargo");
        }

        private bool IsPlayerOnSubwayOrTrain(BasePlayer player)
        {
            if (player == null) return false;
            var parent = player.GetParentEntity();
            if (parent == null) return false;
            var prefabName = parent.PrefabName;
            return prefabName != null && (prefabName.Contains("/subentity") || prefabName.Contains("/traincar"));
        }

        private void StartHealthMonitoring()
        {
            timer.Every(0.1f, () =>
            {
                foreach (var player in activePlayers.ToList())
                {
                    if (player == null || !player.IsConnected) continue;
                    
                    if (!lastHealthValues.ContainsKey(player.userID))
                    {
                        lastHealthValues[player.userID] = player.health;
                        continue;
                    }
                    
                    float currentHealth = player.health;
                    float lastHealth = lastHealthValues[player.userID];
                    
                    // Если здоровье увеличилось не от собственного хила
                    if (currentHealth > lastHealth)
                    {
                        lastExternalHealTime[player.userID] = UnityEngine.Time.realtimeSinceStartup;
                    }
                    
                    lastHealthValues[player.userID] = currentHealth;
                }
            });
        }

        object OnHealingItemUse(MedicalTool medTool, BasePlayer player)
        {
            if (!config.MedicalSpeedDetection.Enabled) return null;
            if (medTool == null || player == null || !player.IsConnected) return null;
            if (permission.UserHasPermission(player.UserIDString, "darkanticheat.ignore.all")) return null;
            if (permission.UserHasPermission(player.UserIDString, "darkanticheat.ignore.medicalspeed")) return null;

            var item = medTool.GetItem();
            if (item == null || item.info.shortname != "syringe.medical") return null;

            var currentTime = UnityEngine.Time.realtimeSinceStartup;

            // Проверяем, получал ли игрок хил от других игроков недавно
            bool recentlyHealedByOthers = false;
            float recentHealThreshold = 1.0f; // Порог времени в секундах для определения недавнего хила
            
            // Проверка на внешний хил (от плагинов или других источников)
            if (lastExternalHealTime.ContainsKey(player.userID) && 
                currentTime - lastExternalHealTime[player.userID] < recentHealThreshold)
            {
                recentlyHealedByOthers = true;
            }
            
            // Проверка на хил от других игроков рядом
            if (!recentlyHealedByOthers)
            {
                foreach (var activePlayer in activePlayers)
                {
                    if (activePlayer == player) continue; // Пропускаем самого игрока
                    
                    // Проверяем дистанцию до других игроков
                    float distance = Vector3.Distance(player.transform.position, activePlayer.transform.position);
                    if (distance <= 3f) // Если игрок находится в пределах 3 метров
                    {
                        // Проверяем, держит ли другой игрок медицинский инструмент
                        var otherHeldEntity = activePlayer.GetHeldEntity();
                        if (otherHeldEntity is MedicalTool)
                        {
                            var otherMedTool = otherHeldEntity as MedicalTool;
                            var otherItem = otherMedTool?.GetItem();
                            
                            if (otherItem != null && (otherItem.info.shortname == "syringe.medical" || 
                                                     otherItem.info.shortname == "bandage" ||
                                                     otherItem.info.shortname == "largemedkit"))
                            {
                                recentlyHealedByOthers = true;
                                break;
                            }
                        }
                    }
                }
            }
            
            if (recentlyHealedByOthers)
            {
                // Если игрок недавно получал хил от других игроков, сбрасываем таймер
                medicalStartTimes[player.userID] = currentTime;
                return null;
            }

            if (!medicalStartTimes.ContainsKey(player.userID))
            {
                medicalStartTimes[player.userID] = currentTime;
                return null;
            }

            float useTime = currentTime - medicalStartTimes[player.userID];
            medicalStartTimes.Remove(player.userID);

            if (useTime < config.MedicalSpeedDetection.MinSyringeUseTime)
            {
                if (!medicalSpeedViolations.ContainsKey(player.userID))
                    medicalSpeedViolations[player.userID] = 0;

                medicalSpeedViolations[player.userID]++;

                if (medicalSpeedViolations[player.userID] >= config.MedicalSpeedDetection.DetectsForBan)
                {
                    Server.Command($"banid {player.UserIDString} {config.MedicalSpeedDetection.BanHours} \"{config.MedicalSpeedDetection.BanReason}\"");

                    if (config.MedicalSpeedDetection.SendDiscordNotification)
                    {
                        SendDiscordMessage(player, config.MedicalSpeedDetection, useTime, 0f);
                    }

                    medicalSpeedViolations.Remove(player.userID);
                }
            }

            return null;
        }

        private void CmdConsoleUnban(ConsoleSystem.Arg arg)
        {
            if (arg.Args == null || arg.Args.Length == 0)
            {
                Puts("Использование: dunban <steam_id>");
                return;
            }

            string steamIdStr = arg.Args[0];
            ulong steamId;

            if (!ulong.TryParse(steamIdStr, out steamId))
            {
                Puts($"Неверный формат Steam ID: {steamIdStr}");
                return;
            }

            if (steamId <= 0)
            {
                Puts("Указан некорректный Steam ID");
                return;
            }

            // Разбан через стандартную систему сервера
            Server.Command($"unban {steamId}");
            ConsoleSystem.Run(ConsoleSystem.Option.Server, "writecfg", null);

            // Дополнительно разбаниваем через систему плагина
            var bannedPlayers = Interface.Oxide.DataFileSystem.ReadObject<Dictionary<string, object>>("darkanticheat_bans") ?? new Dictionary<string, object>();

            bool wasPluginBanned = false;
            if (bannedPlayers.ContainsKey(steamId.ToString()))
            {
                bannedPlayers.Remove(steamId.ToString());
                Interface.Oxide.DataFileSystem.WriteObject("darkanticheat_bans", bannedPlayers);

                if (banTimers.ContainsKey(steamId))
                {
                    banTimers[steamId]?.Destroy();
                    banTimers.Remove(steamId);
                }

                wasPluginBanned = true;
            }

            // Попытка найти имя игрока для лога
            string playerName = "Неизвестно";
            BasePlayer player = BasePlayer.FindByID(steamId);
            if (player != null)
            {
                playerName = player.displayName;
            }

            LogToFile("unbans", $"Администратор разбанил игрока {playerName} (Steam ID: {steamId}) через консоль сервера", this);

            if (wasPluginBanned)
            {
                Puts($"Игрок с Steam ID {steamId} успешно разбанен (был забанен через DarkAntiCheat)");
            }
            else
            {
                Puts($"Игрок с Steam ID {steamId} успешно разбанен");
            }
        }

        private void CmdChatBan(BasePlayer player, string command, string[] args)
        {
            if (player == null) return;

            if (!permission.UserHasPermission(player.UserIDString, "darkanticheat.admin"))
            {
                player.ChatMessage("У вас нет прав на использование этой команды! Требуется право: darkanticheat.admin");
                return;
            }

            if (args.Length < 1)
            {
                player.ChatMessage("Использование: /dban <steamid> [причина]");
                return;
            }

            string targetIdStr = args[0];
            ulong targetId;
            if (!ulong.TryParse(targetIdStr, out targetId) || targetId <= 0)
            {
                player.ChatMessage($"Неверный формат SteamID: {targetIdStr}");
                return;
            }

            // Собираем причину из оставшихся аргументов
            string banReason = args.Length > 1 ? string.Join(" ", args.Skip(1).ToArray()) : "Manual Ban by Admin";

            BasePlayer targetPlayer = BasePlayer.FindByID(targetId);

            if (targetPlayer != null && permission.UserHasPermission(targetPlayer.UserIDString, "darkanticheat.admin"))
            {
                player.ChatMessage($"Вы не можете забанить администратора: {targetPlayer.displayName} ({targetIdStr})");
                return;
            }

            string banCommand;

            if (targetPlayer != null && targetPlayer.IsConnected)
            {
                string ipAddress = targetPlayer.net?.connection?.ipaddress;
                if (!string.IsNullOrEmpty(ipAddress))
                {
                    banCommand = $"banid {targetIdStr} {ipAddress} \"{banReason}\"";
                    player.ChatMessage($"Игрок {targetPlayer.displayName} ({targetIdStr} / {ipAddress}) забанен по причине: {banReason}");
                }
                else
                {
                    banCommand = $"banid {targetIdStr} \"{banReason}\"";
                    player.ChatMessage($"Не удалось получить IP адрес игрока {targetPlayer.displayName} ({targetIdStr}). Игрок забанен только по SteamID по причине: {banReason}");
                }
            }
            else
            {
                // Игрок оффлайн, баним только по SteamID
                banCommand = $"banid {targetIdStr} \"{banReason}\"";
                player.ChatMessage($"Игрок с SteamID {targetIdStr} не найден онлайн. Игрок забанен только по SteamID по причине: {banReason}");
            }

            Server.Command(banCommand);
            ConsoleSystem.Run(ConsoleSystem.Option.Server, "writecfg", null);
            LogToFile("manual_bans", $"Администратор {player.displayName} ({player.UserIDString}) забанил игрока {targetIdStr} через чат.", this);
        }

        private void CmdConsoleBan(ConsoleSystem.Arg arg)
        {
            if (arg.Args == null || arg.Args.Length != 1)
            {
                Puts("Использование: dban <steamid>");
                return;
            }

            string targetIdStr = arg.Args[0];
            ulong targetId;
            if (!ulong.TryParse(targetIdStr, out targetId) || targetId <= 0)
            {
                Puts($"Неверный формат SteamID: {targetIdStr}");
                return;
            }

            BasePlayer targetPlayer = BasePlayer.FindByID(targetId);

            if (targetPlayer != null && permission.UserHasPermission(targetPlayer.UserIDString, "darkanticheat.admin"))
            {
                Puts($"Вы не можете забанить администратора: {targetPlayer.displayName} ({targetIdStr})");
                return;
            }

            string banReason = "Manual Ban by Admin";
            string banCommand;

            if (targetPlayer != null && targetPlayer.IsConnected)
            {
                string ipAddress = targetPlayer.net?.connection?.ipaddress;
                if (!string.IsNullOrEmpty(ipAddress))
                {
                    banCommand = $"banid {targetIdStr} {ipAddress} \"{banReason}\"";
                    Puts($"Игрок {targetPlayer.displayName} ({targetIdStr} / {ipAddress}) забанен.");
                }
                else
                {
                    banCommand = $"banid {targetIdStr} \"{banReason}\"";
                    Puts($"Не удалось получить IP адрес игрока {targetPlayer.displayName} ({targetIdStr}). Игрок забанен только по SteamID.");
                }
            }
            else
            {
                banCommand = $"banid {targetIdStr} \"{banReason}\"";
                Puts($"Игрок с SteamID {targetIdStr} не найден онлайн. Игрок забанен только по SteamID.");
            }

            Server.Command(banCommand);
            ConsoleSystem.Run(ConsoleSystem.Option.Server, "writecfg", null);
            LogToFile("manual_bans", $"Администратор (Console) забанил игрока {targetIdStr} через консоль.", this);
        }
    }
}
