#ifndef SDN_KVS_HEADER_H
#define SDN_KVS_HEADER_H

#include "ns3/header.h"

using namespace ns3;

/**
 * SDN-KVS Header. This header is used as request, response and overloaded signal packet.
 * For client request, only req_id and ks_siz field is used.
 * For server response, all field is used.
 * The heavy field (default 0) will be set to 1, if queue_siz is greater than TQS.
 * A data packet represents a keyset, and a request corresponds to multiple keysets.
 * Each keyset of a request has a different destination.
 */
class SdnKvsHeader : public Header
{
  public:
    SdnKvsHeader();
    ~SdnKvsHeader() override;

    /**
     * Set request id.
     * \param req_id Request id.
     */
    void SetReqId(uint32_t req_id);
    /**
     * Get request id.
     * \return Request id.
     */
    uint32_t GetReqId() const;

    /**
     * Set keyset size.
     * \param ks_siz Keyset size.
     */
    void SetKsSiz(uint32_t ks_siz);
    /**
     * Get keyset size.
     * \return Keyset size.
     */
    uint32_t GetKsSiz() const;

    /**
     * Set queue size.
     * \param queue_siz Queue size.
     */
    void SetQueueSiz(uint32_t queue_siz);
    /**
     * Get queue size.
     * \return Queue size.
     */
    uint32_t GetQueueSiz() const;

    /**
     * Set server rate.
     * \param server_rate Server rate.
     */
    void SetServerRate(uint32_t server_rate);
    /**
     * Get server rate.
     * \return Server rate.
     */
    uint32_t GetServerRate() const;

    /**
     * Set heavy flag.
     * \param heavy Heavy flag.
     */
    void SetHeavy(uint16_t heavy);
    /**
     * Get heavy flag.
     * \return Heavy flag.
     */
    uint16_t GetHeavy() const;

    /**
     * Set Bottleneck keyset size.
     * \param Bottleneck ks_siz Keyset size.
     */
    void SetBottleneckKsSiz(uint32_t bottleneck_ks_siz);
    /**
     * Get Bottleneck keyset size.
     * \return Bottleneck Keyset size.
     */
    uint32_t GetBottleneckKsSiz() const;

    /**
     * Set priority.
     * \param priority Priority.
     */
    void SetPriority(uint32_t priority);
    /**
     * Get priority.
     * \return Priority.
     */
    uint32_t GetPriority() const;

    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;
    void Print(std::ostream& os) const override;
    void Serialize(Buffer::Iterator start) const override;
    uint32_t Deserialize(Buffer::Iterator start) override;
    uint32_t GetSerializedSize() const override;

  private:
    uint32_t m_req_id;            //!< Request id
    uint32_t m_ks_siz;            //!< Keyset size
    uint32_t m_bottleneck_ks_siz; //!< Bottleneck keyset size
    uint32_t m_queue_siz;         //!< Server queue size
    uint32_t m_server_rate;       //!< Server handle rate
    uint16_t m_heavy;             //!< Server heavy status
    /**
     * Request priority. Used by DAS algorithm.
     * It is equal to m_ks_siz if DAS algorithm is not enabled.
     */
    uint32_t m_priority;
};

#endif