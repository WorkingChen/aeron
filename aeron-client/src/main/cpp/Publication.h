/*
 * Copyright 2014-2025 Real Logic Limited.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef AERON_PUBLICATION_H
#define AERON_PUBLICATION_H

#include <array>
#include <memory>
#include <string>

#include "concurrent/logbuffer/BufferClaim.h"
#include "concurrent/logbuffer/HeaderWriter.h"
#include "concurrent/status/UnsafeBufferPosition.h"
#include "concurrent/status/StatusIndicatorReader.h"
#include "LogBuffers.h"
#include "util/Export.h"

namespace aeron
{

using namespace aeron::concurrent::status;

class ClientConductor;

static const std::int64_t NOT_CONNECTED = -1;
static const std::int64_t BACK_PRESSURED = -2;
static const std::int64_t ADMIN_ACTION = -3;
static const std::int64_t PUBLICATION_CLOSED = -4;
static const std::int64_t MAX_POSITION_EXCEEDED = -5;

/**
 * Supplies the reserved value field for a data frame header. The returned value will be set in the header as
 * Little Endian format.
 *
 * This will be called as the last action of encoding a data frame right before the length is set. All other fields
 * in the header plus the body of the frame will have been written at the point of supply.
 *
 * @param termBuffer for the message.
 * @param termOffset of the start of the message.
 * @param length of the message in bytes.
 */
typedef std::function<std::int64_t(
    AtomicBuffer &termBuffer,
    util::index_t termOffset,
    util::index_t length)> on_reserved_value_supplier_t;

static const on_reserved_value_supplier_t DEFAULT_RESERVED_VALUE_SUPPLIER =
    [](AtomicBuffer &, util::index_t, util::index_t) -> std::int64_t { return 0; };

/**
 * @example BasicPublisher.cpp
 */
/**
 * Aeron Publisher API for sending messages to subscribers of a given channel and streamId pair. Publishers
 * are created via an {@link Aeron} object, and messages are sent via an offer method or a claim and commit
 * method combination.
 * <p>
 * The APIs for tryClaim and offer are non-blocking and threadsafe.
 * <p>
 * Note: Publication instances are threadsafe and can be shared between publisher threads.
 * @see Aeron#addPublication
 * @see Aeron#findPublication
 */
class CLIENT_EXPORT Publication
{
public:

    /// @cond HIDDEN_SYMBOLS
    Publication(
        ClientConductor &conductor,
        const std::string &channel,
        std::int64_t registrationId,
        std::int64_t originalRegistrationId,
        std::int32_t streamId,
        std::int32_t sessionId,
        UnsafeBufferPosition &publicationLimit,
        std::int32_t channelStatusId,
        std::shared_ptr<LogBuffers> logBuffers);
    /// @endcond

    ~Publication();

    /**
     * Media address for delivery to the channel.
     *
     * @return Media address for delivery to the channel.
     */
    inline const std::string &channel() const
    {
        return m_channel;
    }

    /**
     * Stream identity for scoping within the channel media address.
     *
     * @return Stream identity for scoping within the channel media address.
     */
    inline std::int32_t streamId() const
    {
        return m_streamId;
    }

    /**
     * Session under which messages are published. Identifies this Publication instance.
     *
     * @return the session id for this publication.
     */
    inline std::int32_t sessionId() const
    {
        return m_sessionId;
    }

    /**
     * The initial term id assigned when this Publication was created. This can be used to determine how many
     * terms have passed since creation.
     *
     * @return the initial term id.
     */
    inline std::int32_t initialTermId() const
    {
        return m_initialTermId;
    }

    /**
     * Get the original registration used to register this Publication with the media driver by the first publisher.
     *
     * @return the original registrationId of the publication.
     */
    inline std::int64_t originalRegistrationId() const
    {
        return m_originalRegistrationId;
    }

    /**
     * Registration Id returned by Aeron::addPublication when this Publication was added.
     *
     * @return the registrationId of the publication.
     */
    inline std::int64_t registrationId() const
    {
        return m_registrationId;
    }

    /**
     * Is this Publication the original instance added to the driver? If not then it was added after another client
     * has already added the publication.
     *
     * @return true if this instance is the first added otherwise false.
     */
    inline bool isOriginal() const
    {
        return m_originalRegistrationId == m_registrationId;
    }

    /**
     * Maximum message length supported in bytes.
     *
     * @return maximum message length supported in bytes.
     */
    inline util::index_t maxMessageLength() const
    {
        return m_maxMessageLength;
    }

    /**
     * Maximum length of a message payload that fits within a message fragment.
     *
     * This is the MTU length minus the message fragment header length.
     *
     * @return maximum message fragment payload length.
     */
    inline util::index_t maxPayloadLength() const
    {
        return m_maxPayloadLength;
    }

    /**
     * Get the length in bytes for each term partition in the log buffer.
     *
     * @return the length in bytes for each term partition in the log buffer.
     */
    inline std::int32_t termBufferLength() const
    {
        return m_logBuffers->atomicBuffer(0).capacity();
    }

    /**
     * Number of bits to right shift a position to get a term count for how far the stream has progressed.
     *
     * @return of bits to right shift a position to get a term count for how far the stream has progressed.
     */
    inline std::int32_t positionBitsToShift() const
    {
        return m_positionBitsToShift;
    }

    /**
     * Has this Publication seen an active subscriber recently?
     *
     * @return true if this Publication has seen an active subscriber recently.
     */
    inline bool isConnected() const
    {
        return !isClosed() && LogBufferDescriptor::isConnected(m_logMetaDataBuffer);
    }

    /**
     * Has this object been closed and should no longer be used?
     *
     * @return true if it has been closed otherwise false.
     */
    inline bool isClosed() const
    {
        return m_isClosed.load(std::memory_order_acquire);
    }

    /**
     * Get the max possible position the stream can reach given term length.
     *
     * @return the max possible position the stream can reach given term length.
     */
    inline std::int64_t maxPossiblePosition() const
    {
        return m_maxPossiblePosition;
    }

    /**
     * Get the current position to which the publication has advanced for this stream.
     *
     * @return the current position to which the publication has advanced for this stream or {@link CLOSED}.
     */
    inline std::int64_t position() const
    {
        std::int64_t result = PUBLICATION_CLOSED;

        if (!isClosed())
        {
            const std::int64_t rawTail = LogBufferDescriptor::rawTailVolatile(m_logMetaDataBuffer);
            const std::int32_t termOffset = LogBufferDescriptor::termOffset(rawTail, termBufferLength());

            result = LogBufferDescriptor::computePosition(
                LogBufferDescriptor::termId(rawTail), termOffset, m_positionBitsToShift, m_initialTermId);
        }

        return result;
    }

    /**
     * Get the position limit beyond which this {@link Publication} will be back pressured.
     *
     * This should only be used as a guide to determine when back pressure is likely to be applied.
     *
     * @return the position limit beyond which this {@link Publication} will be back pressured.
     */
    inline std::int64_t publicationLimit() const
    {
        if (isClosed())
        {
            return PUBLICATION_CLOSED;
        }

        return m_publicationLimit.getVolatile();
    }

    /**
     * Get the counter id used to represent the publication limit.
     *
     * @return the counter id used to represent the publication limit.
     */
    inline std::int32_t publicationLimitId() const
    {
        return m_publicationLimit.id();
    }

    /**
     * Available window for offering into a publication before the {@link #positionLimit()} is reached.
     *
     * @return  window for offering into a publication before the {@link #positionLimit()} is reached. If
     * the publication is closed then {@link #CLOSED} will be returned.
     */
    inline std::int64_t availableWindow() const
    {
        std::int64_t result = PUBLICATION_CLOSED;

        if (!isClosed())
        {
            result = m_publicationLimit.getVolatile() - position();
        }

        return result;
    }

    /**
     * Get the counter id used to represent the channel status.
     *
     * @return the counter id used to represent the channel status.
     */
    inline std::int32_t channelStatusId() const
    {
        return m_channelStatusId;
    }

    /**
     * Get the status for the channel of this {@link Publication}.
     *
     * @return status code for this channel.
     */
    std::int64_t channelStatus() const;

    /**
     * Fetches the local socket addresses for this publication. If the channel is not
     * {@link aeron::concurrent::status::ChannelEndpointStatus::CHANNEL_ENDPOINT_ACTIVE}, then this will return an
     * empty list.
     *
     * The format is as follows:
     * <br>
     * <br>
     * IPv4: <code>ip address:port</code>
     * <br>
     * IPv6: <code>[ip6 address]:port</code>
     * <br>
     * <br>
     * This is to match the formatting used in the Aeron URI
     *
     * @return local socket address for this subscription.
     * @see #channelStatus()
     */
    std::vector<std::string> localSocketAddresses() const;

    /**
     * Non-blocking publish of a buffer containing a message.
     *
     * @param buffer containing message.
     * @param offset offset in the buffer at which the encoded message begins.
     * @param length in bytes of the encoded message.
     * @param reservedValueSupplier for the frame.
     * @return The new stream position, otherwise {@link #NOT_CONNECTED}, {@link #BACK_PRESSURED},
     * {@link #ADMIN_ACTION} or {@link #CLOSED}.
     */
    inline std::int64_t offer(
        const concurrent::AtomicBuffer &buffer,
        util::index_t offset,
        util::index_t length,
        const on_reserved_value_supplier_t &reservedValueSupplier)
    {
        std::int64_t newPosition = PUBLICATION_CLOSED;

        if (!isClosed())
        {
            const std::int64_t limit = m_publicationLimit.getVolatile();
            const std::int32_t termCount = LogBufferDescriptor::activeTermCount(m_logMetaDataBuffer);
            const int partitionIndex = LogBufferDescriptor::indexByTermCount(termCount);
            AtomicBuffer &termBuffer = m_logBuffers->atomicBuffer(partitionIndex);
            const util::index_t tailCounterOffset = LogBufferDescriptor::tailCounterOffset(partitionIndex);
            const std::int64_t rawTail = m_logMetaDataBuffer.getInt64Volatile(tailCounterOffset);
            const std::int32_t termOffset = LogBufferDescriptor::termOffset(rawTail, termBuffer.capacity());
            const std::int32_t termId = LogBufferDescriptor::termId(rawTail);

            if (termCount != LogBufferDescriptor::computeTermCount(termId, m_initialTermId))
            {
                return ADMIN_ACTION;
            }

            const std::int64_t position = LogBufferDescriptor::computePosition(
                termId, termOffset, m_positionBitsToShift, m_initialTermId);
            if (position < limit)
            {
                if (length <= m_maxPayloadLength)
                {
                    newPosition = Publication::appendUnfragmentedMessage(
                        termBuffer, tailCounterOffset, buffer, offset, length, reservedValueSupplier);
                }
                else
                {
                    checkMaxMessageLength(length);
                    newPosition = Publication::appendFragmentedMessage(
                        termBuffer, tailCounterOffset, buffer, offset, length, reservedValueSupplier);
                }
            }
            else
            {
                newPosition = Publication::backPressureStatus(position, length);
            }
        }

        return newPosition;
    }

    /**
     * Non-blocking publish of a buffer containing a message.
     *
     * @param buffer containing message.
     * @param offset offset in the buffer at which the encoded message begins.
     * @param length in bytes of the encoded message.
     * @return The new stream position, otherwise {@link #NOT_CONNECTED}, {@link #BACK_PRESSURED},
     * {@link #ADMIN_ACTION} or {@link #CLOSED}.
     */
    inline std::int64_t offer(const concurrent::AtomicBuffer &buffer, util::index_t offset, util::index_t length)
    {
        return offer(buffer, offset, length, DEFAULT_RESERVED_VALUE_SUPPLIER);
    }

    /**
     * Non-blocking publish of a buffer containing a message.
     *
     * @param buffer containing message.
     * @return The new stream position on success, otherwise {@link BACK_PRESSURED} or {@link NOT_CONNECTED}.
     */
    inline std::int64_t offer(const concurrent::AtomicBuffer &buffer)
    {
        return offer(buffer, 0, buffer.capacity());
    }

    /**
     * Non-blocking publish of buffers containing a message.
     *
     * @param startBuffer containing part of the message.
     * @param lastBuffer after the message.
     * @param reservedValueSupplier for the frame.
     * @return The new stream position, otherwise {@link #NOT_CONNECTED}, {@link #BACK_PRESSURED},
     * {@link #ADMIN_ACTION} or {@link #CLOSED}.
     */
    template<class BufferIterator> std::int64_t offer(
        BufferIterator startBuffer,
        BufferIterator lastBuffer,
        const on_reserved_value_supplier_t &reservedValueSupplier = DEFAULT_RESERVED_VALUE_SUPPLIER)
    {
        util::index_t length = 0;
        for (BufferIterator it = startBuffer; it != lastBuffer; ++it)
        {
            if (AERON_COND_EXPECT(length + it->capacity() < 0, false))
            {
                throw aeron::util::IllegalStateException(
                    "length overflow: " + std::to_string(length) + " + " + std::to_string(it->capacity()) +
                    " > " + std::to_string(length + it->capacity()),
                    SOURCEINFO);
            }

            length += it->capacity();
        }

        std::int64_t newPosition = PUBLICATION_CLOSED;

        if (!isClosed())
        {
            const std::int64_t limit = m_publicationLimit.getVolatile();
            const std::int32_t termCount = LogBufferDescriptor::activeTermCount(m_logMetaDataBuffer);
            const int partitionIndex = LogBufferDescriptor::indexByTermCount(termCount);
            AtomicBuffer &termBuffer = m_logBuffers->atomicBuffer(partitionIndex);
            const util::index_t tailCounterOffset = LogBufferDescriptor::tailCounterOffset(partitionIndex);
            const std::int64_t rawTail = m_logMetaDataBuffer.getInt64Volatile(tailCounterOffset);
            const std::int32_t termOffset = LogBufferDescriptor::termOffset(rawTail, termBuffer.capacity());
            const std::int32_t termId = LogBufferDescriptor::termId(rawTail);

            if (termCount != LogBufferDescriptor::computeTermCount(termId, m_initialTermId))
            {
                return ADMIN_ACTION;
            }

            const std::int64_t position = LogBufferDescriptor::computePosition(
                termId, termOffset, m_positionBitsToShift, m_initialTermId);
            if (position < limit)
            {
                if (length <= m_maxPayloadLength)
                {
                    newPosition = Publication::appendUnfragmentedMessage(
                        termBuffer, tailCounterOffset, startBuffer, length, reservedValueSupplier);
                }
                else
                {
                    checkMaxMessageLength(length);
                    newPosition = Publication::appendFragmentedMessage(
                        termBuffer, tailCounterOffset, startBuffer, length, reservedValueSupplier);
                }
            }
            else
            {
                newPosition = Publication::backPressureStatus(position, length);
            }
        }

        return newPosition;
    }

    /**
     * Non-blocking publish of array of buffers containing a message.
     *
     * @param buffers containing parts of the message.
     * @param length of the array of buffers.
     * @param reservedValueSupplier for the frame.
     * @return The new stream position, otherwise {@link #NOT_CONNECTED}, {@link #BACK_PRESSURED},
     * {@link #ADMIN_ACTION} or {@link #CLOSED}.
     */
    std::int64_t offer(
        const concurrent::AtomicBuffer *buffers,
        std::size_t length,
        const on_reserved_value_supplier_t &reservedValueSupplier = DEFAULT_RESERVED_VALUE_SUPPLIER)
    {
        return offer(buffers, buffers + length, reservedValueSupplier);
    }

    /**
     * Non-blocking publish of array of buffers containing a message.
     *
     * @param buffers containing parts of the message.
     * @param reservedValueSupplier for the frame.
     * @return The new stream position, otherwise {@link #NOT_CONNECTED}, {@link #BACK_PRESSURED},
     * {@link #ADMIN_ACTION} or {@link #CLOSED}.
     */
    template<std::size_t N> std::int64_t offer(
        const std::array<concurrent::AtomicBuffer, N> &buffers,
        const on_reserved_value_supplier_t &reservedValueSupplier = DEFAULT_RESERVED_VALUE_SUPPLIER)
    {
        return offer(buffers.begin(), buffers.end(), reservedValueSupplier);
    }

    /**
     * Try to claim a range in the publication log into which a message can be written with zero copy semantics.
     * Once the message has been written then {@link BufferClaim#commit()} should be called thus making it available.
     * <p>
     * <b>Note:</b> This method can only be used for message lengths less than MTU length minus header.
     *
     * @code
     *     BufferClaim bufferClaim; // Can be stored and reused to avoid allocation
     *
     *     if (publication->tryClaim(messageLength, bufferClaim) > 0)
     *     {
     *         try
     *         {
     *              AtomicBuffer& buffer = bufferClaim.buffer();
     *              const index_t offset = bufferClaim.offset();
     *
     *              // Work with buffer directly or wrap with a flyweight
     *         }
     *         finally
     *         {
     *             bufferClaim.commit();
     *         }
     *     }
     * @endcode
     *
     * @param length      of the range to claim, in bytes..
     * @param bufferClaim to be populate if the claim succeeds.
     * @return The new stream position, otherwise {@link #NOT_CONNECTED}, {@link #BACK_PRESSURED},
     * {@link #ADMIN_ACTION} or {@link #CLOSED}.
     * @throws IllegalArgumentException if the length is greater than max payload length within an MTU.
     * @see BufferClaim::commit
     */
    inline std::int64_t tryClaim(util::index_t length, concurrent::logbuffer::BufferClaim &bufferClaim)
    {
        checkPayloadLength(length);
        std::int64_t newPosition = PUBLICATION_CLOSED;

        if (!isClosed())
        {
            const std::int64_t limit = m_publicationLimit.getVolatile();
            const std::int32_t termCount = LogBufferDescriptor::activeTermCount(m_logMetaDataBuffer);
            const int partitionIndex = LogBufferDescriptor::indexByTermCount(termCount);
            AtomicBuffer &termBuffer = m_logBuffers->atomicBuffer(partitionIndex);
            const util::index_t tailCounterOffset = LogBufferDescriptor::tailCounterOffset(partitionIndex);
            const std::int64_t rawTail = m_logMetaDataBuffer.getInt64Volatile(tailCounterOffset);
            const std::int32_t termOffset = LogBufferDescriptor::termOffset(rawTail, termBuffer.capacity());
            const std::int32_t termId = LogBufferDescriptor::termId(rawTail);

            if (termCount != LogBufferDescriptor::computeTermCount(termId, m_initialTermId))
            {
                return ADMIN_ACTION;
            }

            const std::int64_t position = LogBufferDescriptor::computePosition(
                termId, termOffset, m_positionBitsToShift, m_initialTermId);
            if (position < limit)
            {
                newPosition = Publication::claim(termBuffer, tailCounterOffset, length, bufferClaim);
            }
            else
            {
                newPosition = Publication::backPressureStatus(position, length);
            }
        }

        return newPosition;
    }

    /**
     * Add a destination manually to a multi-destination-cast Publication.
     *
     * @param endpointChannel for the destination to add
     * @return correlation id for the add command
     */
    std::int64_t addDestination(const std::string &endpointChannel);

    /**
     * Remove a previously added destination manually from a multi-destination-cast Publication.
     *
     * @param endpointChannel for the destination to remove
     * @return correlation id for the remove command
     */
    std::int64_t removeDestination(const std::string &endpointChannel);

    /**
     * Retrieve the status of the associated add or remove destination operation with the given correlationId.
     *
     * This method is non-blocking.
     *
     * The value returned is dependent on what has occurred with respect to the media driver:
     *
     * - If the correlationId is unknown, then an exception is thrown.
     * - If the media driver has not answered the add/remove command, then a false is returned.
     * - If the media driver has successfully added or removed the destination then true is returned.
     * - If the media driver has returned an error, this method will throw the error returned.
     *
     * @see Publication::addDestination
     * @see Publication::removeDestination
     *
     * @param correlationId of the add/remove command returned by Publication::addDestination
     * or Publication::removeDestination
     * @return true for added or false if not.
     */
    bool findDestinationResponse(std::int64_t correlationId);

    /// @cond HIDDEN_SYMBOLS
    inline void close()
    {
        m_isClosed.store(true, std::memory_order_release);
    }
    /// @endcond

private:
    ClientConductor &m_conductor;
    AtomicBuffer &m_logMetaDataBuffer;
    const std::string m_channel;
    std::int64_t m_registrationId;
    std::int64_t m_originalRegistrationId;
    std::int64_t m_maxPossiblePosition;
    std::int32_t m_streamId;
    std::int32_t m_sessionId;
    std::int32_t m_initialTermId;
    std::int32_t m_maxPayloadLength;
    std::int32_t m_maxMessageLength;
    std::int32_t m_positionBitsToShift;
    ReadablePosition<UnsafeBufferPosition> m_publicationLimit;
    std::int32_t m_channelStatusId;
    std::atomic<bool> m_isClosed = { false };

    std::shared_ptr<LogBuffers> m_logBuffers;
    HeaderWriter m_headerWriter;

    inline void checkMaxMessageLength(const util::index_t length) const
    {
        if (AERON_COND_EXPECT((length > m_maxMessageLength), false))
        {
            throw aeron::util::IllegalArgumentException(
                "message exceeds maxMessageLength=" + std::to_string(m_maxMessageLength) +
                    ", length=" + std::to_string(length), SOURCEINFO);
        }
    }

    inline void checkPayloadLength(const util::index_t length) const
    {
        if (AERON_COND_EXPECT((length > m_maxPayloadLength), false))
        {
            throw aeron::util::IllegalArgumentException(
                "message exceeds maxPayloadLength=" + std::to_string(m_maxPayloadLength) +
                    ", length=" + std::to_string(length), SOURCEINFO);
        }
    }

    inline std::int64_t claim(
        AtomicBuffer &termBuffer,
        const util::index_t tailCounterOffset,
        const util::index_t length,
        BufferClaim &bufferClaim)
    {
        const util::index_t frameLength = length + DataFrameHeader::LENGTH;
        const util::index_t alignedLength = util::BitUtil::align(frameLength, FrameDescriptor::FRAME_ALIGNMENT);
        const std::int64_t rawTail = m_logMetaDataBuffer.getAndAddInt64(tailCounterOffset, alignedLength);
        const std::int32_t termLength = termBuffer.capacity();
        const std::int32_t termOffset = LogBufferDescriptor::termOffset(rawTail, termLength);
        const std::int32_t termId = LogBufferDescriptor::termId(rawTail);

        const std::int32_t resultingOffset = termOffset + alignedLength;
        const std::int64_t position = LogBufferDescriptor::computePosition(
            termId, resultingOffset, m_positionBitsToShift, m_initialTermId);
        if (resultingOffset > termLength)
        {
            return handleEndOfLogCondition(termBuffer, termOffset, termLength, termId, position);
        }
        else
        {
            m_headerWriter.write(termBuffer, termOffset, frameLength, termId);
            bufferClaim.wrap(termBuffer, termOffset, frameLength);
        }

        return position;
    }

    inline std::int64_t appendUnfragmentedMessage(
        AtomicBuffer &termBuffer,
        const util::index_t tailCounterOffset,
        const AtomicBuffer &srcBuffer,
        util::index_t srcOffset,
        util::index_t length,
        const on_reserved_value_supplier_t &reservedValueSupplier)
    {
        const util::index_t frameLength = length + DataFrameHeader::LENGTH;
        const util::index_t alignedLength = util::BitUtil::align(frameLength, FrameDescriptor::FRAME_ALIGNMENT);
        const std::int64_t rawTail = m_logMetaDataBuffer.getAndAddInt64(tailCounterOffset, alignedLength);
        const std::int32_t termLength = termBuffer.capacity();
        const std::int32_t termOffset = LogBufferDescriptor::termOffset(rawTail, termLength);
        const std::int32_t termId = LogBufferDescriptor::termId(rawTail);

        const std::int32_t resultingOffset = termOffset + alignedLength;
        const std::int64_t position = LogBufferDescriptor::computePosition(
            termId, resultingOffset, m_positionBitsToShift, m_initialTermId);
        if (resultingOffset > termLength)
        {
            return handleEndOfLogCondition(termBuffer, termOffset, termLength, termId, position);
        }
        else
        {
            util::index_t frameOffset = termOffset;
            m_headerWriter.write(termBuffer, frameOffset, frameLength, termId);
            termBuffer.putBytes(frameOffset + DataFrameHeader::LENGTH, srcBuffer, srcOffset, length);

            const std::int64_t reservedValue = reservedValueSupplier(termBuffer, frameOffset, frameLength);
            termBuffer.putInt64(frameOffset + DataFrameHeader::RESERVED_VALUE_FIELD_OFFSET, reservedValue);

            FrameDescriptor::frameLengthOrdered(termBuffer, frameOffset, frameLength);
        }

        return position;
    }

    template <class BufferIterator> std::int64_t appendUnfragmentedMessage(
        AtomicBuffer &termBuffer,
        const util::index_t tailCounterOffset,
        BufferIterator bufferIt,
        util::index_t length,
        const on_reserved_value_supplier_t &reservedValueSupplier)
    {
        const util::index_t frameLength = length + DataFrameHeader::LENGTH;
        const util::index_t alignedLength = util::BitUtil::align(frameLength, FrameDescriptor::FRAME_ALIGNMENT);
        const std::int64_t rawTail = m_logMetaDataBuffer.getAndAddInt64(tailCounterOffset, alignedLength);
        const std::int32_t termLength = termBuffer.capacity();
        const std::int32_t termOffset = LogBufferDescriptor::termOffset(rawTail, termLength);
        const std::int32_t termId = LogBufferDescriptor::termId(rawTail);

        const std::int32_t resultingOffset = termOffset + alignedLength;
        const std::int64_t position = LogBufferDescriptor::computePosition(
            termId, resultingOffset, m_positionBitsToShift, m_initialTermId);
        if (resultingOffset > termLength)
        {
            return handleEndOfLogCondition(termBuffer, termOffset, termLength, termId, position);
        }
        else
        {
            util::index_t frameOffset = termOffset;
            m_headerWriter.write(termBuffer, frameOffset, frameLength, termId);

            std::int32_t offset = frameOffset + DataFrameHeader::LENGTH;
            for (
                std::int32_t endingOffset = offset + length;
                offset < endingOffset;
                offset += bufferIt->capacity(), ++bufferIt)
            {
                termBuffer.putBytes(offset, *bufferIt, 0, bufferIt->capacity());
            }

            const std::int64_t reservedValue = reservedValueSupplier(termBuffer, frameOffset, frameLength);
            termBuffer.putInt64(frameOffset + DataFrameHeader::RESERVED_VALUE_FIELD_OFFSET, reservedValue);

            FrameDescriptor::frameLengthOrdered(termBuffer, frameOffset, frameLength);
        }

        return position;
    }

    std::int64_t appendFragmentedMessage(
        AtomicBuffer &termBuffer,
        const util::index_t tailCounterOffset,
        const AtomicBuffer &srcBuffer,
        util::index_t srcOffset,
        util::index_t length,
        const on_reserved_value_supplier_t &reservedValueSupplier)
    {
        const util::index_t framedLength = LogBufferDescriptor::computeFragmentedFrameLength(
            length, m_maxPayloadLength);
        const std::int64_t rawTail = m_logMetaDataBuffer.getAndAddInt64(tailCounterOffset, framedLength);
        const std::int32_t termLength = termBuffer.capacity();
        const std::int32_t termOffset = LogBufferDescriptor::termOffset(rawTail, termLength);
        const std::int32_t termId = LogBufferDescriptor::termId(rawTail);

        const std::int32_t resultingOffset = termOffset + framedLength;
        const std::int64_t position = LogBufferDescriptor::computePosition(
            termId, resultingOffset, m_positionBitsToShift, m_initialTermId);
        if (resultingOffset > termLength)
        {
            return handleEndOfLogCondition(termBuffer, termOffset, termLength, termId, position);
        }
        else
        {
            std::uint8_t flags = FrameDescriptor::BEGIN_FRAG;
            util::index_t remaining = length;
            util::index_t frameOffset = termOffset;

            do
            {
                const util::index_t bytesToWrite = std::min(remaining, m_maxPayloadLength);
                const util::index_t frameLength = bytesToWrite + DataFrameHeader::LENGTH;
                const util::index_t alignedLength =
                    util::BitUtil::align(frameLength, FrameDescriptor::FRAME_ALIGNMENT);

                m_headerWriter.write(termBuffer, frameOffset, frameLength, termId);
                termBuffer.putBytes(
                    frameOffset + DataFrameHeader::LENGTH,
                    srcBuffer,
                    srcOffset + (length - remaining),
                    bytesToWrite);

                if (remaining <= m_maxPayloadLength)
                {
                    flags |= FrameDescriptor::END_FRAG;
                }

                FrameDescriptor::frameFlags(termBuffer, frameOffset, flags);

                const std::int64_t reservedValue = reservedValueSupplier(termBuffer, frameOffset, frameLength);
                termBuffer.putInt64(frameOffset + DataFrameHeader::RESERVED_VALUE_FIELD_OFFSET, reservedValue);

                FrameDescriptor::frameLengthOrdered(termBuffer, frameOffset, frameLength);

                flags = 0;
                frameOffset += alignedLength;
                remaining -= bytesToWrite;
            }
            while (remaining > 0);
        }

        return position;
    }

    template <class BufferIterator> std::int64_t appendFragmentedMessage(
        AtomicBuffer &termBuffer,
        const util::index_t tailCounterOffset,
        BufferIterator bufferIt,
        util::index_t length,
        const on_reserved_value_supplier_t &reservedValueSupplier)
    {
        const util::index_t framedLength = LogBufferDescriptor::computeFragmentedFrameLength(
            length, m_maxPayloadLength);
        const std::int64_t rawTail = m_logMetaDataBuffer.getAndAddInt64(tailCounterOffset, framedLength);
        const std::int32_t termLength = termBuffer.capacity();
        const std::int32_t termOffset = LogBufferDescriptor::termOffset(rawTail, termLength);
        const std::int32_t termId = LogBufferDescriptor::termId(rawTail);

        const std::int32_t resultingOffset = termOffset + framedLength;
        const std::int64_t position = LogBufferDescriptor::computePosition(
            termId, resultingOffset, m_positionBitsToShift, m_initialTermId);
        if (resultingOffset > termLength)
        {
            return handleEndOfLogCondition(termBuffer, termOffset, termLength, termId, position);
        }
        else
        {
            std::uint8_t flags = FrameDescriptor::BEGIN_FRAG;
            util::index_t remaining = length;
            util::index_t frameOffset = termOffset;
            util::index_t currentBufferOffset = 0;

            do
            {
                const util::index_t bytesToWrite = std::min(remaining, m_maxPayloadLength);
                const util::index_t frameLength = bytesToWrite + DataFrameHeader::LENGTH;
                const util::index_t alignedLength = util::BitUtil::align(frameLength, FrameDescriptor::FRAME_ALIGNMENT);

                m_headerWriter.write(termBuffer, frameOffset, frameLength, termId);

                util::index_t bytesWritten = 0;
                util::index_t payloadOffset = frameOffset + DataFrameHeader::LENGTH;
                do
                {
                    const util::index_t currentBufferRemaining = bufferIt->capacity() - currentBufferOffset;
                    const util::index_t numBytes = std::min(bytesToWrite - bytesWritten, currentBufferRemaining);

                    termBuffer.putBytes(payloadOffset, *bufferIt, currentBufferOffset, numBytes);

                    bytesWritten += numBytes;
                    payloadOffset += numBytes;
                    currentBufferOffset += numBytes;

                    if (currentBufferRemaining <= numBytes)
                    {
                        ++bufferIt;
                        currentBufferOffset = 0;
                    }
                }
                while (bytesWritten < bytesToWrite);

                if (remaining <= m_maxPayloadLength)
                {
                    flags |= FrameDescriptor::END_FRAG;
                }

                FrameDescriptor::frameFlags(termBuffer, frameOffset, flags);

                const std::int64_t reservedValue = reservedValueSupplier(termBuffer, frameOffset, frameLength);
                termBuffer.putInt64(frameOffset + DataFrameHeader::RESERVED_VALUE_FIELD_OFFSET, reservedValue);

                FrameDescriptor::frameLengthOrdered(termBuffer, frameOffset, frameLength);

                flags = 0;
                frameOffset += alignedLength;
                remaining -= bytesToWrite;
            }
            while (remaining > 0);
        }

        return position;
    }

    inline std::int64_t handleEndOfLogCondition(
        AtomicBuffer &termBuffer,
        std::int32_t termOffset,
        std::int32_t termLength,
        std::int32_t termId,
        std::int64_t position)
    {
        if (termOffset < termLength)
        {
            const std::int32_t paddingLength = termLength - termOffset;
            m_headerWriter.write(termBuffer, termOffset, paddingLength, termId);
            FrameDescriptor::frameType(termBuffer, termOffset, DataFrameHeader::HDR_TYPE_PAD);
            FrameDescriptor::frameLengthOrdered(termBuffer, termOffset, paddingLength);
        }

        if (position >= m_maxPossiblePosition)
        {
            return MAX_POSITION_EXCEEDED;
        }

        const auto termCount = LogBufferDescriptor::computeTermCount(termId, m_initialTermId);
        LogBufferDescriptor::rotateLog(m_logMetaDataBuffer, termCount, termId);

        return ADMIN_ACTION;
    }

    inline std::int64_t backPressureStatus(std::int64_t currentPosition, std::int32_t messageLength)
    {
        if ((currentPosition + util::BitUtil::align(
            messageLength + DataFrameHeader::LENGTH, FrameDescriptor::FRAME_ALIGNMENT)) >= m_maxPossiblePosition)
        {
            return MAX_POSITION_EXCEEDED;
        }

        if (LogBufferDescriptor::isConnected(m_logMetaDataBuffer))
        {
            return BACK_PRESSURED;
        }

        return NOT_CONNECTED;
    }
};

}

#endif
