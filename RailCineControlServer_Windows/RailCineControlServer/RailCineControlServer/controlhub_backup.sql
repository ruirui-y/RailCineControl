-- MySQL dump 10.13  Distrib 8.0.42, for Win64 (x86_64)
--
-- Host: localhost    Database: controlhub
-- ------------------------------------------------------
-- Server version	8.0.42

/*!40101 SET @OLD_CHARACTER_SET_CLIENT=@@CHARACTER_SET_CLIENT */;
/*!40101 SET @OLD_CHARACTER_SET_RESULTS=@@CHARACTER_SET_RESULTS */;
/*!40101 SET @OLD_COLLATION_CONNECTION=@@COLLATION_CONNECTION */;
/*!50503 SET NAMES utf8mb4 */;
/*!40103 SET @OLD_TIME_ZONE=@@TIME_ZONE */;
/*!40103 SET TIME_ZONE='+00:00' */;
/*!40014 SET @OLD_UNIQUE_CHECKS=@@UNIQUE_CHECKS, UNIQUE_CHECKS=0 */;
/*!40014 SET @OLD_FOREIGN_KEY_CHECKS=@@FOREIGN_KEY_CHECKS, FOREIGN_KEY_CHECKS=0 */;
/*!40101 SET @OLD_SQL_MODE=@@SQL_MODE, SQL_MODE='NO_AUTO_VALUE_ON_ZERO' */;
/*!40111 SET @OLD_SQL_NOTES=@@SQL_NOTES, SQL_NOTES=0 */;

--
-- Current Database: `controlhub`
--

CREATE DATABASE /*!32312 IF NOT EXISTS*/ `controlhub` /*!40100 DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci */ /*!80016 DEFAULT ENCRYPTION='N' */;

USE `controlhub`;

--
-- Table structure for table `game_info`
--

DROP TABLE IF EXISTS `game_info`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE `game_info` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT COMMENT '游戏库主键ID',
  `game_name` varchar(100) COLLATE utf8mb4_general_ci NOT NULL COMMENT '游戏展示名称',
  `version` varchar(50) COLLATE utf8mb4_general_ci NOT NULL COMMENT '当前版本号 (如 v1.0.0)',
  `description` text COLLATE utf8mb4_general_ci COMMENT '游戏玩法或简介',
  `cover_url` varchar(255) COLLATE utf8mb4_general_ci DEFAULT NULL COMMENT '海报图片在服务器的相对路径',
  `package_md5` char(32) COLLATE utf8mb4_general_ci NOT NULL COMMENT '游戏 .tar 压缩包的 MD5 值 (用于切片下载和完整性校验)',
  `package_size` bigint unsigned NOT NULL DEFAULT '0' COMMENT '压缩包总大小(字节)',
  `exe_path` varchar(255) COLLATE utf8mb4_general_ci NOT NULL COMMENT '解压后的相对启动路径 (如 WindowsNoEditor/MyGame/Binaries/Win64/Game.exe)',
  `create_time` datetime DEFAULT CURRENT_TIMESTAMP COMMENT '首次上传入库时间',
  `update_time` datetime DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '最后一次更新版本的时间',
  PRIMARY KEY (`id`),
  UNIQUE KEY `idx_game_name` (`game_name`) USING BTREE COMMENT '游戏名防止重复录入',
  KEY `idx_package_md5` (`package_md5`) USING BTREE COMMENT '加速根据MD5查找包信息'
) ENGINE=InnoDB AUTO_INCREMENT=3 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='全局游戏资源库';
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `game_info`
--

LOCK TABLES `game_info` WRITE;
/*!40000 ALTER TABLE `game_info` DISABLE KEYS */;
INSERT INTO `game_info` VALUES (2,'MiNiWorld','1.0.1','第一次上传','./UploadedAssets/Game/3d5831f4de28260ef08029cc5a7e0275_cover.jpg','3d5831f4de28260ef08029cc5a7e0275',9091731968,'MiNiWorld.exe','2026-05-14 18:27:39','2026-05-14 18:27:39');
/*!40000 ALTER TABLE `game_info` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `sys_account`
--

DROP TABLE IF EXISTS `sys_account`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE `sys_account` (
  `id` int NOT NULL AUTO_INCREMENT COMMENT '主键ID',
  `username` varchar(50) NOT NULL COMMENT '登录账号 (厂家分配)',
  `password` varchar(64) NOT NULL COMMENT '密码值',
  `shop_name` varchar(100) DEFAULT NULL COMMENT '所属门店/客户名称 (方便你后台对账管理)',
  `expire_time` datetime NOT NULL COMMENT '账号授权到期时间 (到期自动拒绝登录)',
  `status` tinyint(1) NOT NULL DEFAULT '1' COMMENT '状态: 1=正常, 0=封禁/停用 (遇到欠款客户一键停机)',
  `current_mac` varchar(50) DEFAULT NULL COMMENT '当前登录设备的MAC地址 (用于顶号或设备绑定)',
  `last_login_time` datetime DEFAULT NULL COMMENT '最后活跃时间 (结合心跳包更新)',
  `last_heartbeat_time` datetime DEFAULT NULL COMMENT '最后一次有效心跳时间',
  `user_permissions` tinyint NOT NULL DEFAULT '0' COMMENT '权限级别: 0=普通门店客户端(仅浏览/播放), 1=超级管理员(允许上传影片)',
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_username` (`username`)
) ENGINE=InnoDB AUTO_INCREMENT=2 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci COMMENT='中控主机授权账号表';
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `sys_account`
--

LOCK TABLES `sys_account` WRITE;
/*!40000 ALTER TABLE `sys_account` DISABLE KEYS */;
INSERT INTO `sys_account` VALUES (1,'admin','123456',NULL,'2027-04-01 00:00:00',1,NULL,'2026-05-21 14:16:20','2000-01-01 00:00:00',1);
/*!40000 ALTER TABLE `sys_account` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `t_file_audit_log`
--

DROP TABLE IF EXISTS `t_file_audit_log`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE `t_file_audit_log` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT COMMENT '告警主键ID',
  `user_id` int NOT NULL COMMENT '触发告警的用户/商户ID',
  `file_md5` varchar(64) NOT NULL COMMENT '涉事文件的 MD5 (如果有)',
  `movie_name` varchar(100) DEFAULT NULL COMMENT '关联的影片名称 (通过内存映射匹配)',
  `action_type` varchar(50) NOT NULL COMMENT '行为类型 (如: 恶意复制, 非法重命名, 文件被删)',
  `detail` varchar(255) DEFAULT NULL COMMENT '详细描述 (如新生成的文件名)',
  `create_time` datetime DEFAULT CURRENT_TIMESTAMP COMMENT '触发时间',
  PRIMARY KEY (`id`),
  KEY `idx_audit_user` (`user_id`),
  CONSTRAINT `fk_audit_user` FOREIGN KEY (`user_id`) REFERENCES `sys_account` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci COMMENT='终端安全与文件审计告警表';
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `t_file_audit_log`
--

LOCK TABLES `t_file_audit_log` WRITE;
/*!40000 ALTER TABLE `t_file_audit_log` DISABLE KEYS */;
/*!40000 ALTER TABLE `t_file_audit_log` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `t_goods_sku`
--

DROP TABLE IF EXISTS `t_goods_sku`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE `t_goods_sku` (
  `goods_id` bigint NOT NULL AUTO_INCREMENT COMMENT '商品套餐ID',
  `name` varchar(100) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '套餐名称 (如"100点券", "首充特惠")',
  `price_cents` int NOT NULL COMMENT '人民币价格 (单位：分，严禁使用浮点数)',
  `points_reward` int NOT NULL COMMENT '购买后给予的积分数量',
  `status` tinyint NOT NULL DEFAULT '1' COMMENT '状态: 0=已下架, 1=正常上架',
  `create_time` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
  PRIMARY KEY (`goods_id`)
) ENGINE=InnoDB AUTO_INCREMENT=1005 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='充值套餐商品表';
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `t_goods_sku`
--

LOCK TABLES `t_goods_sku` WRITE;
/*!40000 ALTER TABLE `t_goods_sku` DISABLE KEYS */;
INSERT INTO `t_goods_sku` VALUES (1,'10元=100积分',1000,100,1,'2026-05-15 10:27:53'),(2,'首充特惠：50元=600积分',5000,600,1,'2026-05-15 10:27:53'),(3,'土豪专属：100元=1500积分',10000,1500,1,'2026-05-15 10:27:53'),(1001,'10元=100积分',1000,100,1,'2026-05-15 11:56:00'),(1002,'首充特惠：50元=600积分',5000,600,1,'2026-05-15 11:56:00'),(1003,'尊享特惠：100元=1500积分',10000,1500,1,'2026-05-15 11:56:00'),(1004,'超级特惠：500元=8000积分',50000,8000,1,'2026-05-15 11:56:00');
/*!40000 ALTER TABLE `t_goods_sku` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `t_movie_record`
--

DROP TABLE IF EXISTS `t_movie_record`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE `t_movie_record` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT COMMENT '记录主键ID',
  `user_id` int NOT NULL COMMENT '关联 sys_account 的 id',
  `movie_name` varchar(100) NOT NULL COMMENT '影片名称',
  `play_date` date NOT NULL COMMENT '播放日期 (YYYY-MM-DD)',
  `start_time` time NOT NULL COMMENT '开始时间 (HH:mm:ss)',
  `end_time` time NOT NULL COMMENT '结束时间 (HH:mm:ss)',
  `operator_name` varchar(50) DEFAULT NULL COMMENT '操作员姓名',
  `end_type` varchar(20) DEFAULT NULL COMMENT '结束类型 (如: 正常结束, 强制结束)',
  `create_time` datetime DEFAULT CURRENT_TIMESTAMP COMMENT '数据入库时间',
  PRIMARY KEY (`id`),
  KEY `idx_user_date` (`user_id`,`play_date`),
  CONSTRAINT `fk_record_user` FOREIGN KEY (`user_id`) REFERENCES `sys_account` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB AUTO_INCREMENT=41 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci COMMENT='播放历史记录表';
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `t_movie_record`
--

LOCK TABLES `t_movie_record` WRITE;
/*!40000 ALTER TABLE `t_movie_record` DISABLE KEYS */;
INSERT INTO `t_movie_record` VALUES (4,1,'1','2026-04-09','14:41:20','14:41:31','admin','强制结束','2026-04-09 14:41:31'),(5,1,'1','2026-04-09','15:41:57','15:42:08','admin','正常结束','2026-04-09 15:42:08'),(6,1,'1','2026-04-09','15:42:12','15:42:14','admin','正常结束','2026-04-09 15:42:14'),(7,1,'1','2026-04-09','15:42:21','15:42:24','admin','正常结束','2026-04-09 15:42:24'),(8,1,'1','2026-04-09','15:42:26','15:42:45','admin','正常结束','2026-04-09 15:42:45'),(9,1,'1','2026-04-09','15:42:51','15:43:10','admin','正常结束','2026-04-09 15:43:10'),(10,1,'1','2026-04-09','15:45:13','15:45:27','admin','正常结束','2026-04-09 15:45:27'),(11,1,'hutting','2026-04-09','15:56:34','16:00:12','admin','正常结束','2026-04-09 16:00:12'),(12,1,'hutting','2026-04-13','10:40:14','10:40:54','admin','强制结束','2026-04-13 10:40:54'),(13,1,'2','2026-04-13','10:42:53','10:43:19','admin','正常结束','2026-04-13 10:43:19'),(14,1,'hutting','2026-04-20','16:29:46','16:33:25','admin','正常结束','2026-04-20 16:33:25'),(15,1,'hutting','2026-04-20','16:48:43','16:48:55','admin','强制结束','2026-04-20 16:48:55'),(16,1,'hutting','2026-04-20','16:52:41','16:53:09','admin','强制结束','2026-04-20 16:53:09'),(17,1,'1','2026-04-20','16:53:11','16:53:19','admin','强制结束','2026-04-20 16:53:19'),(18,1,'2','2026-04-20','16:53:21','16:53:27','admin','强制结束','2026-04-20 16:53:27'),(21,1,'mengyan','2026-04-22','16:13:53','16:14:03','admin','强制结束','2026-04-22 16:14:03'),(22,1,'Behemoth','2026-04-22','16:14:07','16:14:20','admin','强制结束','2026-04-22 16:14:20'),(23,1,'hutting','2026-04-22','16:14:24','16:14:35','admin','强制结束','2026-04-22 16:14:35'),(24,1,'huanyou','2026-04-22','16:58:10','16:58:21','admin','强制结束','2026-04-22 16:58:21'),(25,1,'huanyou','2026-04-22','17:10:51','17:10:54','admin','强制结束','2026-04-22 17:10:54'),(26,1,'mengyan','2026-04-22','17:10:57','17:11:01','admin','强制结束','2026-04-22 17:11:01'),(27,1,'hutting','2026-04-22','17:11:03','17:11:14','admin','强制结束','2026-04-22 17:11:14'),(28,1,'huanyou','2026-04-22','17:14:43','17:14:45','admin','强制结束','2026-04-22 17:14:45'),(29,1,'mengyan','2026-04-22','17:14:47','17:14:49','admin','强制结束','2026-04-22 17:14:49'),(30,1,'hutting','2026-04-22','17:14:51','17:14:54','admin','强制结束','2026-04-22 17:14:54'),(31,1,'huanyou','2026-04-22','17:29:46','17:29:48','admin','强制结束','2026-04-22 17:29:48'),(32,1,'mengyan','2026-04-22','17:29:50','17:29:54','admin','强制结束','2026-04-22 17:29:54'),(33,1,'hutting','2026-04-22','17:29:55','17:30:05','admin','强制结束','2026-04-22 17:30:05'),(34,1,'Behemoth','2026-04-22','17:30:07','17:30:15','admin','强制结束','2026-04-22 17:30:15'),(35,1,'huanyou','2026-04-22','17:30:17','17:30:21','admin','强制结束','2026-04-22 17:30:21'),(36,1,'mengyan','2026-04-22','17:30:22','17:30:26','admin','强制结束','2026-04-22 17:30:26'),(37,1,'mengyan','2026-04-22','18:17:59','18:21:46','admin','强制结束','2026-04-22 18:21:46'),(38,1,'2086','2026-04-22','18:22:08','18:22:23','admin','强制结束','2026-04-22 18:22:23'),(39,1,'2086','2026-04-22','11:53:40','11:54:01','admin','强制结束','2026-04-27 11:54:01'),(40,1,'2086','2026-04-22','18:51:28','18:51:31','admin','强制结束','2026-05-14 18:51:31');
/*!40000 ALTER TABLE `t_movie_record` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `t_movie_resource`
--

DROP TABLE IF EXISTS `t_movie_resource`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE `t_movie_resource` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT COMMENT '资源全局自增主键',
  `file_md5` varchar(64) NOT NULL COMMENT '核心防重：视频文件的 MD5 或 SHA256 哈希值 (用于实现“秒传”)',
  `original_name` varchar(100) NOT NULL COMMENT '影片初始名称 (第一个上传此文件的用户命名的名字)',
  `cover_url` varchar(500) DEFAULT NULL COMMENT '云端海报 CDN 绝对链接',
  `video_url` varchar(500) NOT NULL COMMENT '云端视频源 CDN 绝对链接',
  `description` varchar(500) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci DEFAULT NULL COMMENT '影片简介',
  `file_size` bigint DEFAULT '0' COMMENT '文件大小(字节数)，便于客户端计算下载进度',
  `duration_sec` int DEFAULT '0' COMMENT '视频总时长(秒)',
  `encrypt_key` varchar(32) DEFAULT NULL COMMENT '头部 1MB XOR 混淆的影片专属随机密钥',
  `upload_by` bigint DEFAULT NULL COMMENT '首次上传该资源的用户 ID (溯源用)',
  `create_time` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '资源入库时间',
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_file_md5` (`file_md5`) COMMENT '唯一索引：确保整个云端同一个 MD5 文件绝对不会存两份'
) ENGINE=InnoDB AUTO_INCREMENT=21 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci COMMENT='全局影片云端资源池';
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `t_movie_resource`
--

LOCK TABLES `t_movie_resource` WRITE;
/*!40000 ALTER TABLE `t_movie_resource` DISABLE KEYS */;
INSERT INTO `t_movie_resource` VALUES (20,'6b3a799de46aaafdd649d1a337a49c35','2086','./UploadedAssets/Movie/6b3a799de46aaafdd649d1a337a49c35_cover.jpg','./UploadedAssets/Movie/6b3a799de46aaafdd649d1a337a49c35.mp4','2086',1085249391,143,'1ace3bfa1abe46d5b4f810ea12a092f4',1,'2026-05-14 17:41:32');
/*!40000 ALTER TABLE `t_movie_resource` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `t_pay_order`
--

DROP TABLE IF EXISTS `t_pay_order`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE `t_pay_order` (
  `order_id` varchar(64) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '系统内部订单号 (如 PAY20260515xxx)',
  `third_party_no` varchar(128) COLLATE utf8mb4_unicode_ci DEFAULT NULL COMMENT '微信/支付宝回调的真实流水号 (用于对账)',
  `user_id` bigint NOT NULL COMMENT '购买用户的ID',
  `goods_id` bigint NOT NULL COMMENT '购买的商品套餐ID',
  `amount_cents` int NOT NULL COMMENT '实际支付金额 (单位：分)',
  `status` tinyint NOT NULL DEFAULT '0' COMMENT '状态: 0=待支付, 1=已支付, -1=已关闭/作废, -2=异常挂起(需人工核实)',
  `create_time` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '订单创建时间',
  `pay_time` datetime DEFAULT NULL COMMENT '实际付款时间 (微信/支付宝回调时更新)',
  PRIMARY KEY (`order_id`),
  KEY `idx_user_id` (`user_id`),
  KEY `idx_third_party_no` (`third_party_no`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='支付订单主表';
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `t_pay_order`
--

LOCK TABLES `t_pay_order` WRITE;
/*!40000 ALTER TABLE `t_pay_order` DISABLE KEYS */;
INSERT INTO `t_pay_order` VALUES ('PAY20260519170527_1',NULL,1,1,1000,-1,'2026-05-19 17:05:27',NULL),('PAY20260519170937_1',NULL,1,1,1000,-1,'2026-05-19 17:09:37',NULL),('PAY20260519171818_1',NULL,1,1,1000,-1,'2026-05-19 17:18:18',NULL),('PAY20260519172951_1',NULL,1,1,1000,-1,'2026-05-19 17:29:51',NULL),('PAY20260519173000_1',NULL,1,1,1000,-1,'2026-05-19 17:30:00',NULL),('PAY20260519173115_1',NULL,1,1,1000,-1,'2026-05-19 17:31:15',NULL),('PAY20260519173116_1',NULL,1,1,1000,-1,'2026-05-19 17:31:16',NULL),('PAY20260519174118_1',NULL,1,1,1000,-1,'2026-05-19 17:41:18',NULL),('PAY20260519174206_1',NULL,1,1,1000,-1,'2026-05-19 17:42:06',NULL),('PAY20260519174555_1',NULL,1,1,1000,-1,'2026-05-19 17:45:55',NULL),('PAY20260519175541_1',NULL,1,1,1000,-1,'2026-05-19 17:55:41',NULL),('PAY20260519180306_1',NULL,1,1,1000,-1,'2026-05-19 18:03:06',NULL),('PAY20260519180518_1',NULL,1,1,1000,-1,'2026-05-19 18:05:18',NULL),('PAY20260519181228_1',NULL,1,1,1000,-1,'2026-05-19 18:12:28',NULL),('PAY20260519181817_1',NULL,1,1,1000,-1,'2026-05-19 18:18:17',NULL),('PAY20260519181930_1',NULL,1,1,1000,-1,'2026-05-19 18:19:30',NULL),('PAY20260519181958_1',NULL,1,1,1000,-1,'2026-05-19 18:19:58',NULL),('PAY20260519182001_1',NULL,1,1001,1000,-1,'2026-05-19 18:20:01',NULL),('PAY20260519182005_1',NULL,1,2,5000,-1,'2026-05-19 18:20:05',NULL),('PAY20260519182117_1',NULL,1,1,1000,-1,'2026-05-19 18:21:17',NULL),('PAY20260519182429_1',NULL,1,1,1000,-1,'2026-05-19 18:24:29',NULL),('PAY20260520170445_1',NULL,1,1,1000,-1,'2026-05-20 17:04:45',NULL),('PAY20260520174724_1',NULL,1,1,1000,-1,'2026-05-20 17:47:24',NULL),('PAY20260520175156_1',NULL,1,1,1000,-1,'2026-05-20 17:51:56',NULL),('PAY20260520175214_1',NULL,1,1001,1000,-1,'2026-05-20 17:52:14',NULL),('PAY20260520175216_1',NULL,1,2,5000,-1,'2026-05-20 17:52:16',NULL),('PAY20260521112039_1',NULL,1,1,1000,-2,'2026-05-21 11:20:39',NULL),('PAY20260521112716_1','4200003029202605217036332370',1,1,1000,1,'2026-05-21 11:27:16','2026-05-21 14:15:18'),('PAY20260521141535_1',NULL,1,1,1000,0,'2026-05-21 14:15:35',NULL);
/*!40000 ALTER TABLE `t_pay_order` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `t_point_flow`
--

DROP TABLE IF EXISTS `t_point_flow`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE `t_point_flow` (
  `flow_id` bigint NOT NULL AUTO_INCREMENT COMMENT '流水自增主键',
  `user_id` bigint NOT NULL COMMENT '发生变动的用户ID',
  `flow_type` tinyint NOT NULL COMMENT '流水类型: 1=充值收入, 2=看电影支出, 3=玩游戏支出, 4=系统赠送/退款',
  `points_change` int NOT NULL COMMENT '变动额度 (正数代表增加，负数代表扣除)',
  `balance_after` bigint NOT NULL COMMENT '变动后的最终余额 (极度重要：用于排查账目死角)',
  `associated_id` varchar(64) COLLATE utf8mb4_unicode_ci DEFAULT NULL COMMENT '关联ID (充值对应订单号，消费对应电影/游戏ID)',
  `create_time` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '流水发生时间',
  PRIMARY KEY (`flow_id`),
  KEY `idx_user_flow` (`user_id`,`create_time`)
) ENGINE=InnoDB AUTO_INCREMENT=4 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='资金/积分流水记录表';
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `t_point_flow`
--

LOCK TABLES `t_point_flow` WRITE;
/*!40000 ALTER TABLE `t_point_flow` DISABLE KEYS */;
INSERT INTO `t_point_flow` VALUES (1,1,1,600,1250,'PAY20260515140001','2026-05-15 14:05:00'),(2,1,2,-50,1200,'MOVIE_9921','2026-05-15 14:30:00'),(3,1,1,100,1350,'PAY20260521112716_1','2026-05-21 14:15:18');
/*!40000 ALTER TABLE `t_point_flow` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `t_user_movie_rel`
--

DROP TABLE IF EXISTS `t_user_movie_rel`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE `t_user_movie_rel` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT COMMENT '关系主键',
  `user_id` bigint NOT NULL COMMENT '用户/商户 ID',
  `movie_id` bigint unsigned NOT NULL COMMENT '关联的 t_movie_resource 表的主键 ID',
  `custom_name` varchar(100) DEFAULT NULL COMMENT '用户自定义别名 (允许张三叫“深海浩劫”，李四叫“海底大冒险”)',
  `play_status` tinyint NOT NULL DEFAULT '0' COMMENT '设备当前状态 (0:空闲, 1:播放中)',
  `sort_order` int NOT NULL DEFAULT '0' COMMENT '用户个人的自定义 UI 排序权重',
  `auth_status` tinyint NOT NULL DEFAULT '1' COMMENT '授权状态 (1:已授权正常拉取, 2:已过期冻结)',
  `create_time` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '用户获取/关联该影片的时间',
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_user_movie` (`user_id`,`movie_id`) COMMENT '联合唯一约束：一个用户不能重复关联同一个物理影片',
  KEY `idx_user_id` (`user_id`) COMMENT '方便快速查询某用户的所有影片'
) ENGINE=InnoDB AUTO_INCREMENT=19 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci COMMENT='用户影片授权与私人配置关联表';
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `t_user_movie_rel`
--

LOCK TABLES `t_user_movie_rel` WRITE;
/*!40000 ALTER TABLE `t_user_movie_rel` DISABLE KEYS */;
INSERT INTO `t_user_movie_rel` VALUES (18,1,20,'2086',0,0,1,'2026-05-14 17:41:32');
/*!40000 ALTER TABLE `t_user_movie_rel` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `t_user_wallet`
--

DROP TABLE IF EXISTS `t_user_wallet`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE `t_user_wallet` (
  `user_id` bigint NOT NULL COMMENT '用户ID，作为主键',
  `balance_points` bigint NOT NULL DEFAULT '0' COMMENT '当前可用积分',
  `total_recharged` bigint NOT NULL DEFAULT '0' COMMENT '历史总充值积分',
  `total_consumed` bigint NOT NULL DEFAULT '0' COMMENT '历史总消费积分',
  `updated_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '最后更新时间(乐观锁/防并发)',
  PRIMARY KEY (`user_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='用户钱包表';
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `t_user_wallet`
--

LOCK TABLES `t_user_wallet` WRITE;
/*!40000 ALTER TABLE `t_user_wallet` DISABLE KEYS */;
INSERT INTO `t_user_wallet` VALUES (1,1350,1600,250,'2026-05-21 14:15:18');
/*!40000 ALTER TABLE `t_user_wallet` ENABLE KEYS */;
UNLOCK TABLES;
/*!40103 SET TIME_ZONE=@OLD_TIME_ZONE */;

/*!40101 SET SQL_MODE=@OLD_SQL_MODE */;
/*!40014 SET FOREIGN_KEY_CHECKS=@OLD_FOREIGN_KEY_CHECKS */;
/*!40014 SET UNIQUE_CHECKS=@OLD_UNIQUE_CHECKS */;
/*!40101 SET CHARACTER_SET_CLIENT=@OLD_CHARACTER_SET_CLIENT */;
/*!40101 SET CHARACTER_SET_RESULTS=@OLD_CHARACTER_SET_RESULTS */;
/*!40101 SET COLLATION_CONNECTION=@OLD_COLLATION_CONNECTION */;
/*!40111 SET SQL_NOTES=@OLD_SQL_NOTES */;

-- Dump completed on 2026-05-21 14:44:50
