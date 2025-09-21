package org.ystre.dsp;

import io.netty.bootstrap.ServerBootstrap;
import io.netty.channel.ChannelInitializer;
import io.netty.channel.ChannelOption;
import io.netty.channel.EventLoopGroup;
import io.netty.channel.MultiThreadIoEventLoopGroup;
import io.netty.channel.nio.NioIoHandler;
import io.netty.channel.socket.SocketChannel;
import io.netty.channel.socket.nio.NioServerSocketChannel;
import io.netty.handler.codec.LengthFieldBasedFrameDecoder;

import jakarta.annotation.PostConstruct;
import jakarta.annotation.PreDestroy;

import java.nio.ByteOrder;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Component;

@Component
public class TcpServer {
    @Value("${tcp.server.port:7200}")
    private int port;

    private EventLoopGroup bossGroup;
    private EventLoopGroup workerGroup;

    @PostConstruct
    public void start() throws InterruptedException {
        bossGroup = new MultiThreadIoEventLoopGroup(1, NioIoHandler.newFactory());  // Accepts connections
        workerGroup = new MultiThreadIoEventLoopGroup(NioIoHandler.newFactory());   // Handles I/O

        try {
            ServerBootstrap bootstrap = new ServerBootstrap()
                .group(bossGroup, workerGroup)
                .channel(NioServerSocketChannel.class)
                .option(ChannelOption.SO_BACKLOG, 128)
                .childHandler(new ChannelInitializer<SocketChannel>() {
                    @Override
                    protected void initChannel(SocketChannel ch) {
                        ch.pipeline().addLast(
                            new DebugHandler(),
                            new LengthFieldBasedFrameDecoder(
                                ByteOrder.BIG_ENDIAN,
                                1024,   // Max frame length (adjust as needed)
                                0,      // Length field offset
                                2,      // Length field length (2 bytes)
                                0,      // Length adjustment
                                2,      // Bytes to strip (skip length field)
                                true    // Fail on too-large frame
                            ),
                            new MessageHandler()
                        );
                    }
                });

            bootstrap.bind(port).sync();
            System.out.println("TCP Server started on port " + port);
        } catch (Exception e) {
            shutdown();
            throw e;
        }
    }

    @PreDestroy
    public void shutdown() {
        if (workerGroup != null) {
            workerGroup.shutdownGracefully();
        }

        if (bossGroup != null) {
            bossGroup.shutdownGracefully();
        }

        System.out.println("TCP Server stopped!");
    }
};
