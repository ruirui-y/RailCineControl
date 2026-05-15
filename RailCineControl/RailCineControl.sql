-- 设置字符集，防止中文乱码
SET NAMES utf8mb4;

-- ========================================================
-- 1. 用户钱包表 (t_user_wallet)
-- ========================================================
CREATE TABLE `t_user_wallet` (
  `user_id` BIGINT NOT NULL COMMENT '用户ID，作为主键',
  `balance_points` BIGINT NOT NULL DEFAULT 0 COMMENT '当前可用积分',
  `total_recharged` BIGINT NOT NULL DEFAULT 0 COMMENT '历史总充值积分',
  `total_consumed` BIGINT NOT NULL DEFAULT 0 COMMENT '历史总消费积分',
  `updated_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '最后更新时间(乐观锁/防并发)',
  PRIMARY KEY (`user_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='用户钱包表';

-- ========================================================
-- 2. 充值套餐/商品表 (t_goods_sku)
-- ========================================================
CREATE TABLE `t_goods_sku` (
  `goods_id` BIGINT NOT NULL AUTO_INCREMENT COMMENT '商品套餐ID',
  `name` VARCHAR(100) NOT NULL COMMENT '套餐名称 (如"100点券", "首充特惠")',
  `price_cents` INT NOT NULL COMMENT '人民币价格 (单位：分，严禁使用浮点数)',
  `points_reward` INT NOT NULL COMMENT '购买后给予的积分数量',
  `status` TINYINT NOT NULL DEFAULT 1 COMMENT '状态: 0=已下架, 1=正常上架',
  `create_time` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
  PRIMARY KEY (`goods_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='充值套餐商品表';

-- 插入几条初始测试数据
INSERT INTO `t_goods_sku` (`name`, `price_cents`, `points_reward`, `status`) VALUES 
('10元=100积分', 1000, 100, 1),
('首充特惠：50元=600积分', 5000, 600, 1),
('土豪专属：100元=1500积分', 10000, 1500, 1);

-- ========================================================
-- 3. 支付订单主表 (t_pay_order) 👑 最重要
-- ========================================================
CREATE TABLE `t_pay_order` (
  `order_id` VARCHAR(64) NOT NULL COMMENT '系统内部订单号 (如 PAY20260515xxx)',
  `third_party_no` VARCHAR(128) DEFAULT NULL COMMENT '微信/支付宝回调的真实流水号 (用于对账)',
  `user_id` BIGINT NOT NULL COMMENT '购买用户的ID',
  `goods_id` BIGINT NOT NULL COMMENT '购买的商品套餐ID',
  `amount_cents` INT NOT NULL COMMENT '实际支付金额 (单位：分)',
  `status` TINYINT NOT NULL DEFAULT 0 COMMENT '状态: 0=待支付, 1=已支付, 2=已取消, 3=已退款',
  `create_time` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '订单创建时间',
  `pay_time` DATETIME DEFAULT NULL COMMENT '实际付款时间 (微信/支付宝回调时更新)',
  PRIMARY KEY (`order_id`),
  KEY `idx_user_id` (`user_id`),           -- 方便查询某人的所有订单
  KEY `idx_third_party_no` (`third_party_no`) -- 方便退款或对账时按流水号反查
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='支付订单主表';

-- ========================================================
-- 4. 资金流水记录表 (t_point_flow)
-- ========================================================
CREATE TABLE `t_point_flow` (
  `flow_id` BIGINT NOT NULL AUTO_INCREMENT COMMENT '流水自增主键',
  `user_id` BIGINT NOT NULL COMMENT '发生变动的用户ID',
  `flow_type` TINYINT NOT NULL COMMENT '流水类型: 1=充值收入, 2=看电影支出, 3=玩游戏支出, 4=系统赠送/退款',
  `points_change` INT NOT NULL COMMENT '变动额度 (正数代表增加，负数代表扣除)',
  `balance_after` BIGINT NOT NULL COMMENT '变动后的最终余额 (极度重要：用于排查账目死角)',
  `associated_id` VARCHAR(64) DEFAULT NULL COMMENT '关联ID (充值对应订单号，消费对应电影/游戏ID)',
  `create_time` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '流水发生时间',
  PRIMARY KEY (`flow_id`),
  KEY `idx_user_flow` (`user_id`, `create_time`) -- 复合索引：极其方便查询某人按时间排序的账单
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='资金/积分流水记录表';