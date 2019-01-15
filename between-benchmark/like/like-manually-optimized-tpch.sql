SELECT 100.00 * SUM(case when p_type >= 'PROMO' then l_extendedprice*(1-l_discount) else 0 end) / SUM(l_extendedprice * (1 - l_discount)) as promo_revenue FROM lineitem, part WHERE l_partkey = p_partkey AND l_shipdate >= ? AND l_shipdate < ?;