-- 设置字符集
SET NAMES utf8mb4;

-- 1. 为测试用户初始化钱包 (假设测试用户 ID 为 1)
-- 初始状态：1250 积分，总充值 1500，总消费 250
INSERT INTO `t_user_wallet` (`user_id`, `balance_points`, `total_recharged`, `total_consumed`) 
VALUES (1, 1250, 1500, 250)
ON DUPLICATE KEY UPDATE 
    `balance_points` = VALUES(`balance_points`),
    `total_recharged` = VALUES(`total_recharged`),
    `total_consumed` = VALUES(`total_consumed`);

-- 2. 插入充值套餐 (t_goods_sku) 
-- 注意：如果之前已经插入过，这里会忽略，保证不重复
INSERT IGNORE INTO `t_goods_sku` (`goods_id`, `name`, `price_cents`, `points_reward`, `status`) VALUES 
(1001, '10元=100积分', 1000, 100, 1),
(1002, '首充特惠：50元=600积分', 5000, 600, 1),
(1003, '尊享特惠：100元=1500积分', 10000, 1500, 1),
(1004, '超级特惠：500元=8000积分', 50000, 8000, 1);

-- 3. 插入模拟支付订单 (t_pay_order)
-- 模拟一笔已支付的订单和一笔待支付的订单
INSERT INTO `t_pay_order` (`order_id`, `third_party_no`, `user_id`, `goods_id`, `amount_cents`, `status`, `create_time`, `pay_time`) VALUES 
('PAY20260515140001', 'WX20260515_888888', 1, 1002, 5000, 1, '2026-05-15 14:00:00', '2026-05-15 14:05:00'),
('PAY20260515150002', NULL, 1, 1001, 1000, 0, '2026-05-15 15:00:00', NULL);

-- 4. 插入资金流水记录 (t_point_flow)
-- 模拟两条流水：一笔充值收入，一笔看电影消费
INSERT INTO `t_point_flow` (`user_id`, `flow_type`, `points_change`, `balance_after`, `associated_id`, `create_time`) VALUES 
(1, 1, 600, 1250, 'PAY20260515140001', '2026-05-15 14:05:00'),
(1, 2, -50, 1200, 'MOVIE_9921', '2026-05-15 14:30:00');