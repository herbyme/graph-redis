# Directed Graphs

require 'redis'

redis = Redis.new

describe 'Minimum spanning tree' do

  it 'should calculate the correct minimum spanning tree' do

    redis.flushdb
    redis.gvertex 'graph1', 'a', 'b', 'c'
    redis.gsetdirected 'graph1'
    redis.gedge 'graph1', 'a', 'b', 1
    redis.gedge 'graph1', 'b', 'a', 4
    redis.gedge 'graph1', 'b', 'c', 2
    redis.gedge 'graph1', 'c', 'b', 3

    path1 = redis.gshortestpath 'graph1', 'a', 'c'
    path2 = redis.gshortestpath 'graph1', 'c', 'a'

    path1.last.should eq '3'
    path2.last.should eq '7'

  end

end

